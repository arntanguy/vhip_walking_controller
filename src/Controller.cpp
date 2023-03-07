/*
 * Copyright (c) 2018-2019, CNRS-UM LIRMM
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <mc_rbdyn/rpy_utils.h>

#include <vhip_walking/Controller.h>
#include <vhip_walking/utils/clamp.h>

namespace vhip_walking
{
  Controller::Controller(std::shared_ptr<mc_rbdyn::RobotModule> robotModule, double dt, const mc_rtc::Configuration & config)
    : mc_control::fsm::Controller(robotModule, dt, config),
      halfSitPose(controlRobot().mbc().q),
      floatingBaseObs_(controlRobot()),
      comVelFilter_(dt, /* cutoff period = */ 0.01),
      netWrenchObs_(),
      stabilizer_(controlRobot(), pendulum_, dt)
  {
    auto robotConfig = config("robot_models")(controlRobot().name());

    // Patch CoM height and step width in all plans
    std::vector<std::string> plans = config("plans").keys();
    double comHeight = robotConfig("com")("height");
    double stepWidth = robotConfig("step_width");
    maxCoMHeight_ = robotConfig("com")("max_height");
    minCoMHeight_ = robotConfig("com")("min_height");
    for (const auto & p : plans)
    {
      auto plan = config("plans")(p);
      if (!plan.has("com_height"))
      {
        plan.add("com_height", comHeight);
      }
      if (plan.has("contacts"))
      {
        for (auto contact : plan("contacts"))
        {
          std::string surf = contact("surface");
          Eigen::Vector3d trans = contact("pose")("translation");
          trans.y() = ((surf == "LeftFootCenter") ? +0.5 : -0.5) * stepWidth;
          contact("pose").add("translation", trans);
        }
      }
    }

    // Add upper-body tasks
    double pelvisStiffness = config("tasks")("pelvis")("stiffness");
    double pelvisWeight = config("tasks")("pelvis")("weight");
    std::string pelvisBodyName = robot().mb().body(0).name();
    pelvisTask = std::make_shared<mc_tasks::OrientationTask>(pelvisBodyName, robots(), 0);
    pelvisTask->orientation(pelvisOrientation_);
    pelvisTask->stiffness(pelvisStiffness);
    pelvisTask->weight(pelvisWeight);

    double postureStiffness = config("tasks")("posture")("stiffness");
    double postureWeight = config("tasks")("posture")("weight");
    postureTask = getPostureTask(robot().name());
    postureTask->stiffness(postureStiffness);
    postureTask->weight(postureWeight);

    std::string torsoName = robotConfig("torso");
    double torsoStiffness = config("tasks")("torso")("stiffness");
    double torsoWeight = config("tasks")("torso")("weight");
    config("tasks")("torso")("pitch", defaultTorsoPitch_);
    torsoPitch_ = defaultTorsoPitch_;
    torsoTask = std::make_shared<mc_tasks::OrientationTask>(torsoName, robots(), 0);
    torsoTask->orientation(mc_rbdyn::rpyToMat({0, torsoPitch_, 0}) * pelvisOrientation_);
    torsoTask->stiffness(torsoStiffness);
    torsoTask->weight(torsoWeight);

    // Set half-sitting pose for posture task
    const auto & halfSit = robotModule->stance();
    const auto & refJointOrder = robot().refJointOrder();
    for (unsigned i = 0; i < refJointOrder.size(); ++i)
    {
      if (robot().hasJoint(refJointOrder[i]))
      {
        halfSitPose[robot().jointIndexByName(refJointOrder[i])] = halfSit.at(refJointOrder[i]);
      }
    }

    // Read settings from configuration file
    plans_ = config("plans");
    mpcConfig_ = config("mpc");
    sole_ = robotConfig("sole");
    std::string initialPlan = plans_.keys()[0];
    config("initial_plan", initialPlan);

    std::vector<std::string> comActiveJoints = robotConfig("com")("active_joints");
    config("stabilizer").add("admittance", robotConfig("admittance"));
    config("stabilizer")("tasks")("com").add("active_joints", comActiveJoints);
    stabilizer_.configure(config("stabilizer"));

    if (robotConfig.has("force_calib"))
    {
      netWrenchObs_.forceCalib(robotConfig("force_calib"));
    }

    loadFootstepPlan(initialPlan);
    stabilizer_.reset(robots());
    stabilizer_.wrenchFaceMatrix(sole_);

    addLogEntries(logger());
    mpc_.addLogEntries(logger());
    netWrenchObs_.addLogEntries(logger());
    stabilizer_.addLogEntries(logger());

    if (gui_)
    {
      addGUIElements(gui_);
      mpc_.addGUIElements(gui_);
      stabilizer_.addGUIElements(gui_);
    }

    mc_rtc::log::success("VHIPWalking controller init done.");
  }

  void Controller::addLogEntries(mc_rtc::Logger & logger)
  {
    logger.addLogEntry("controlRobot_LeftFoot", [this]() { return controlRobot().surfacePose("LeftFoot"); });
    logger.addLogEntry("controlRobot_LeftFootCenter", [this]() { return controlRobot().surfacePose("LeftFootCenter"); });
    logger.addLogEntry("controlRobot_RightFoot", [this]() { return controlRobot().surfacePose("RightFoot"); });
    logger.addLogEntry("controlRobot_RightFootCenter", [this]() { return controlRobot().surfacePose("RightFootCenter"); });
    logger.addLogEntry("controlRobot_com", [this]() { return controlCom_; });
    logger.addLogEntry("controlRobot_comd", [this]() { return controlComd_; });
    logger.addLogEntry("controlRobot_comd_norm", [this]() { return controlComd_.norm(); });
    logger.addLogEntry("controlRobot_dcm", [this]() -> Eigen::Vector3d { return controlCom_ + controlComd_ / pendulum_.omega(); });
    logger.addLogEntry("controlRobot_posW", [this]() { return controlRobot().posW(); });
    logger.addLogEntry("left_foot_ratio", [this]() { return leftFootRatio_; });
    logger.addLogEntry("left_foot_ratio_measured", [this]() { return measuredLeftFootRatio(); });
    logger.addLogEntry("mpc_failures", [this]() { return nbMPCFailures_; });
    logger.addLogEntry("mpc_weights_jerk", [this]() { return mpc_.jerkWeight; });
    logger.addLogEntry("mpc_weights_vel", [this]() { return mpc_.velWeights; });
    logger.addLogEntry("mpc_weights_zmp", [this]() { return mpc_.zmpWeight; });
    logger.addLogEntry("pendulum_com", [this]() { return pendulum_.com(); });
    logger.addLogEntry("pendulum_comd", [this]() { return pendulum_.comd(); });
    logger.addLogEntry("pendulum_comdd", [this]() { return pendulum_.comdd(); });
    logger.addLogEntry("pendulum_dcm", [this]() { return pendulum_.dcm(); });
    logger.addLogEntry("pendulum_omega", [this]() { return pendulum_.omega(); });
    logger.addLogEntry("pendulum_zmp", [this]() { return pendulum_.zmp(); });
    logger.addLogEntry("plan_com_height", [this]() { return plan.comHeight(); });
    logger.addLogEntry("plan_double_support_duration", [this]() { return plan.doubleSupportDuration(); });
    logger.addLogEntry("plan_final_dsp_duration", [this]() { return plan.finalDSPDuration(); });
    logger.addLogEntry("plan_init_dsp_duration", [this]() { return plan.initDSPDuration(); });
    logger.addLogEntry("plan_landing_duration", [this]() { return plan.landingDuration(); });
    logger.addLogEntry("plan_landing_pitch", [this]() { return plan.landingPitch(); });
    logger.addLogEntry("plan_ref_vel", [this]() { return plan.supportContact().refVel; });
    logger.addLogEntry("plan_single_support_duration", [this]() { return plan.singleSupportDuration(); });
    logger.addLogEntry("plan_swing_height", [this]() { return plan.swingHeight(); });
    logger.addLogEntry("plan_takeoff_duration", [this]() { return plan.takeoffDuration(); });
    logger.addLogEntry("plan_takeoff_offset", [this]() { return plan.takeoffOffset(); });
    logger.addLogEntry("plan_takeoff_pitch", [this]() { return plan.takeoffPitch(); });
    logger.addLogEntry("realRobot_LeftFoot", [this]() { return realRobot().surfacePose("LeftFoot"); });
    logger.addLogEntry("realRobot_LeftFootCenter", [this]() { return realRobot().surfacePose("LeftFootCenter"); });
    logger.addLogEntry("realRobot_RightFoot", [this]() { return realRobot().surfacePose("RightFoot"); });
    logger.addLogEntry("realRobot_RightFootCenter", [this]() { return realRobot().surfacePose("RightFootCenter"); });
    logger.addLogEntry("realRobot_com", [this]() { return realCom_; });
    logger.addLogEntry("realRobot_comd", [this]() { return realComd_; });
    logger.addLogEntry("realRobot_dcm", [this]() -> Eigen::Vector3d { return realCom_ + realComd_ / pendulum_.omega(); });
    logger.addLogEntry("realRobot_posW", [this]() { return realRobot().posW(); });
    logger.addLogEntry("realRobot_wrench", [this]() { return netWrenchObs_.wrench(); });
    logger.addLogEntry("realRobot_zmp", [this]() { return netWrenchObs_.zmp(); });
  }

  void Controller::reset(const mc_control::ControllerResetData & data)
  {
    mc_control::fsm::Controller::reset(data);
    if (gui_)
    {
      gui_->removeCategory({"Contacts"});
    }
  }

  void Controller::internalReset()
  {
    // (1) update floating-base transforms of both robot mbc's
    auto X_0_fb = supportContact().robotTransform(controlRobot());
    controlRobot().posW(X_0_fb);
    controlRobot().velW(sva::MotionVecd::Zero());
    realRobot().posW(X_0_fb);
    realRobot().velW(sva::MotionVecd::Zero());

    // (2) update contact frames to coincide with surface ones
    loadFootstepPlan(plan.name);

    // (3) reset solver tasks
    postureTask->posture(halfSitPose);
    solver().removeTask(pelvisTask);
    solver().removeTask(torsoTask);
    stabilizer_.reset(robots());

    // (4) reset controller attributes
    controlCom_ = controlRobot().com();
    controlComd_ = Eigen::Vector3d::Zero();
    leftFootRatioJumped_ = true;
    leftFootRatio_ = 0.5;
    nbMPCFailures_ = 0;
    pauseWalking = false;
    pauseWalkingRequested = false;

    comVelFilter_.reset(controlCom_);
    pendulum_.reset(controlCom_);

    // (5) reset floating-base observers
    floatingBaseObs_.reset(controlRobot().posW());
    floatingBaseObs_.leftFootRatio(leftFootRatio_);
    floatingBaseObs_.run(realRobot());
    updateRealFromKinematics(); // after leftFootRatio_ is initialized

    // (6) updates that depend on realCom_
    netWrenchObs_.update(realRobot(), supportContact());
    stabilizer_.updateState(realCom_, realComd_, netWrenchObs_.wrench(), leftFootRatio_);

    stopLogSegment();
  }

  void Controller::leftFootRatio(double ratio)
  {
    double maxRatioVar = 1.5 * timeStep / plan.doubleSupportDuration();
    if (std::abs(ratio - leftFootRatio_) > maxRatioVar)
    {
      mc_rtc::log::warning("Left foot ratio jumped from {} to {}", leftFootRatio_, ratio);
      leftFootRatioJumped_ = true;
    }
    leftFootRatio_ = clamp(ratio, 0., 1., "leftFootRatio");
  }

  bool Controller::run()
  {
    if (emergencyStop)
    {
      return false;
    }
    if (pauseWalkingRequested)
    {
      pauseWalkingCallback();
    }
    if (!mc_control::fsm::Controller::running())
    {
      return mc_control::fsm::Controller::run();
    }

    controlCom_ = controlRobot().com();
    controlComd_ = controlRobot().comVelocity();
    ctlTime_ += timeStep;

    warnIfRobotIsInTheAir();

    floatingBaseObs_.leftFootRatio(leftFootRatio_);
    floatingBaseObs_.run(realRobot());
    updateRealFromKinematics();
    sva::PTransformd X_0_a = floatingBaseObs_.getAnchorFrame(controlRobot());
    pelvisOrientation_ = X_0_a.rotation();
    pelvisTask->orientation(pelvisOrientation_);
    torsoTask->orientation(mc_rbdyn::rpyToMat({0, torsoPitch_, 0}) * pelvisOrientation_);

    netWrenchObs_.updateAnchorFrame(stabilizer_.contactState(), controlRobot());
    netWrenchObs_.update(realRobot(), supportContact());
    stabilizer_.updateState(realCom_, realComd_, netWrenchObs_.wrench(), leftFootRatio_);

    bool ret = mc_control::fsm::Controller::run();
    if (mc_control::fsm::Controller::running())
    {
      postureTask->posture(halfSitPose); // reset posture in case the FSM updated it
    }
    return ret;
  }

  void Controller::pauseWalkingCallback(bool verbose)
  {
    constexpr double MAX_HEIGHT_DIFF = 0.02; // [m]
    if (pauseWalking)
    {
      mc_rtc::log::warning("Already pausing, how did you get there?");
      return;
    }
    else if (std::abs(supportContact().z() - targetContact().z()) > MAX_HEIGHT_DIFF)
    {
      if (!pauseWalkingRequested || verbose)
      {
        mc_rtc::log::warning("Cannot pause on uneven ground, will pause later");
      }
      gui()->removeElement({"Walking", "Controller"}, "Pause walking");
      pauseWalkingRequested = true;
    }
    else if (pauseWalkingRequested)
    {
      mc_rtc::log::warning("Pausing now that contacts are at same level");
      pauseWalkingRequested = false;
      pauseWalking = true;
    }
    else // (!pauseWalkingRequested)
    {
      gui()->removeElement({"Walking", "Controller"}, "Pause walking");
      pauseWalking = true;
    }
  }

  void Controller::warnIfRobotIsInTheAir()
  {
    static bool isInTheAir = false;
    constexpr double CONTACT_THRESHOLD = 30.; // [N]
    double leftFootPressure = realRobot().forceSensor("LeftFootForceSensor").force().z();
    double rightFootPressure = realRobot().forceSensor("RightFootForceSensor").force().z();
    if (leftFootPressure < CONTACT_THRESHOLD && rightFootPressure < CONTACT_THRESHOLD)
    {
      if (!isInTheAir)
      {
        mc_rtc::log::warning("Robot is in the air");
        isInTheAir = true;
      }
    }
    else
    {
      if (isInTheAir)
      {
        mc_rtc::log::info("Robot is on the ground again");
        isInTheAir = false;
      }
    }
  }

  void Controller::updateRealFromKinematics()
  {
    floatingBaseObs_.updateRobot(realRobot());
    realCom_ = realRobot().com();
    if (!leftFootRatioJumped_)
    {
      comVelFilter_.update(realCom_);
    }
    else // don't update velocity when CoM position jumped
    {
      comVelFilter_.updatePositionOnly(realCom_);
      leftFootRatioJumped_ = false;
    }
    realComd_ = comVelFilter_.vel();
  }

  void Controller::loadFootstepPlan(std::string name)
  {
    double initHeight = (plan.name.length() > 0) ? plan.supportContact().p().z() : 0.;

    plan = plans_(name);
    plan.name = name;
    mpc_.configure(mpcConfig_);
    if (!plan.mpcConfig.empty())
    {
      mpc_.configure(plan.mpcConfig);
    }
    plan.complete(sole_);
    const sva::PTransformd & X_0_lc = controlRobot().surfacePose("LeftFootCenter");
    const sva::PTransformd & X_0_rc = controlRobot().surfacePose("RightFootCenter");
    plan.updateInitialTransform(X_0_lc, X_0_rc, initHeight);
    plan.rewind();
    torsoPitch_ = (plan.hasTorsoPitch()) ? plan.torsoPitch() : defaultTorsoPitch_;
    mc_rtc::log::info("Loaded footstep plan \"{}\"", name);
  }

  void Controller::startLogSegment(const std::string & label)
  {
    if (segmentName_.length() > 0)
    {
      stopLogSegment();
    }
    segmentName_ = "t_" + std::to_string(++nbLogSegments_).erase(0, 1) + "_" + label;
    logger().addLogEntry(segmentName_, [this]() { return ctlTime_; });
  }

  void Controller::stopLogSegment()
  {
    logger().removeLogEntry(segmentName_);
    segmentName_ = "";
  }

  bool Controller::updatePreview()
  {
    mpc_.initState(pendulum());
    mpc_.comHeight(plan.comHeight());
    if (mpc_.solve())
    {
      preview = mpc_.solution();
      return true;
    }
    else
    {
      nbMPCFailures_++;
      return false;
    }
  }
}
