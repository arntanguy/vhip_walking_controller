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

#include <chrono>

#include <vhip_walking/Stabilizer.h>
#include <vhip_walking/utils/clamp.h>
#include <mc_rtc/gui.h>

namespace vhip_walking
{
  namespace
  {
    inline Eigen::Vector3d roundVec(const Eigen::Vector3d & vec, double fact = 1.)
    {
      double x = std::round(vec.x() * fact) / fact;
      double y = std::round(vec.y() * fact) / fact;
      double z = std::round(vec.z() * fact) / fact;
      return Eigen::Vector3d{x, y, z};
    }

    static const std::string TEMPLATE_MODEL_LABELS[2] =
    {
      "Linear inverted pendulum",
      "Variable height inverted pendulum"
    };

    inline const std::string & templateModelToString(TemplateModel & templateModel)
    {
      unsigned i = (templateModel == TemplateModel::LinearInvertedPendulum) ? 0 : 1;
      return TEMPLATE_MODEL_LABELS[i];
    }

    inline TemplateModel templateModelFromString(const std::string & str)
    {
      if (str == TEMPLATE_MODEL_LABELS[0])
      {
        return TemplateModel::LinearInvertedPendulum;
      }
      else
      {
        return TemplateModel::VariableHeightInvertedPendulum;
      }
    }
  }

  Stabilizer::Stabilizer(const mc_rbdyn::Robot & controlRobot, const Pendulum & pendulum, double dt)
    : dcmIntegrator_(dt, /* timeConstant = */ 5.),
      pendulum_(pendulum),
      controlRobot_(controlRobot),
      dt_(dt),
      mass_(controlRobot.mass())
  {
  }

  void Stabilizer::addLogEntries(mc_rtc::Logger & logger)
  {
    logger.addLogEntry("stabilizer_contactState",
      [this]() -> double
      {
        switch (contactState_)
        {
          case ContactState::DoubleSupport:
            return 0;
          case ContactState::LeftFoot:
            return 1;
          case ContactState::RightFoot:
            return -1;
          default:
            return -3;
        }
      });
    logger.addLogEntry("error_com", [this]() -> Eigen::Vector3d { return pendulum_.com() - measuredCoM_; });
    logger.addLogEntry("error_comd", [this]() -> Eigen::Vector3d { return pendulum_.comd() - measuredCoMd_; });
    logger.addLogEntry("error_dcm", [this]() { return dcmError_; });
    logger.addLogEntry("error_dcmAverage", [this]() { return dcmAverageError_; });
    logger.addLogEntry("error_dfz", [this]() { return logTargetDFz_ - logMeasuredDFz_; });
    logger.addLogEntry("error_sfz", [this]() { return logTargetSTz_ - logMeasuredSTz_; });
    logger.addLogEntry("perf_Stabilizer_fdqp", [this]() { return fdqpRunTime_; });
    logger.addLogEntry("perf_Stabilizer_run", [this]() { return runTime_; });
    logger.addLogEntry("perf_Stabilizer_vhip", [this]() { return vhipRunTime_; });
    logger.addLogEntry("stabilizer_admittance_com", [this]() { return comAdmittance_; });
    logger.addLogEntry("stabilizer_admittance_cop", [this]() { return copAdmittance_; });
    logger.addLogEntry("stabilizer_admittance_dfz", [this]() { return dfzAdmittance_; });
    logger.addLogEntry("stabilizer_altcc_comAccel", [this]() { return altccCoMAccel_; });
    logger.addLogEntry("stabilizer_altcc_comOffset", [this]() { return altccCoMOffset_; });
    logger.addLogEntry("stabilizer_altcc_comVel", [this]() { return altccCoMVel_; });
    logger.addLogEntry("stabilizer_altcc_error", [this]() { return altccError_; });
    logger.addLogEntry("stabilizer_altcc_leakRate", [this]() { return altccIntegrator_.rate(); });
    logger.addLogEntry("stabilizer_comOffset", [this]() { return comOffset_; });
    logger.addLogEntry("stabilizer_dcm_feedback_gain", [this]() { return dcmGain_; });
    logger.addLogEntry("stabilizer_dcm_feedback_integralGain", [this]() { return dcmIntegralGain_; });
    logger.addLogEntry("stabilizer_distribWrench", [this]() { return distribWrench_; });
    logger.addLogEntry("stabilizer_fdqp_weights_ankleTorque", [this]() { return std::pow(fdqpWeights_.ankleTorqueSqrt, 2); });
    logger.addLogEntry("stabilizer_fdqp_weights_netWrench", [this]() { return std::pow(fdqpWeights_.netWrenchSqrt, 2); });
    logger.addLogEntry("stabilizer_fdqp_weights_pressure", [this]() { return std::pow(fdqpWeights_.pressureSqrt, 2); });
    logger.addLogEntry("stabilizer_integrator_timeConstant", [this]() { return dcmIntegrator_.timeConstant(); });
    logger.addLogEntry("stabilizer_lambda_distrib", [this]() { return distribLambda_; });
    logger.addLogEntry("stabilizer_lambda_max", [this]() { return lambdaMax_; });
    logger.addLogEntry("stabilizer_lambda_measured", [this]() { return measuredLambda_; });
    logger.addLogEntry("stabilizer_lambda_min", [this]() { return lambdaMin_; });
    logger.addLogEntry("stabilizer_vdc_damping", [this]() { return vdcDamping_; });
    logger.addLogEntry("stabilizer_vdc_frequency", [this]() { return vdcFrequency_; });
    logger.addLogEntry("stabilizer_vdc_stiffness", [this]() { return vdcStiffness_; });
    logger.addLogEntry("stabilizer_vdc_z_pos", [this]() { return vdcZPos_; });
    logger.addLogEntry("stabilizer_vfc_dfz_measured", [this]() { return logMeasuredDFz_; });
    logger.addLogEntry("stabilizer_vfc_dfz_target", [this]() { return logTargetDFz_; });
    logger.addLogEntry("stabilizer_vfc_stz_measured", [this]() { return logMeasuredSTz_; });
    logger.addLogEntry("stabilizer_vfc_stz_target", [this]() { return logTargetSTz_; });
    logger.addLogEntry("stabilizer_vfc_z_ctrl", [this]() { return vfcZCtrl_; });
    logger.addLogEntry("stabilizer_vhip_dcm", [this]() { return vhipDCM_; });
    logger.addLogEntry("stabilizer_vhip_lambda", [this]() { return vhipLambda_; });
    logger.addLogEntry("stabilizer_vhip_omega", [this]() { return vhipOmega_; });
    logger.addLogEntry("stabilizer_vhip_omega2", [this]() { return std::pow(vhipOmega_, 2); });
    logger.addLogEntry("stabilizer_vhip_zmp", [this]() { return vhipZMP_; });
    logger.addLogEntry("stabilizer_zmp", [this]() { return zmp(); });
    logger.addLogEntry("stabilizer_zmpcc_comAccel", [this]() { return zmpccCoMAccel_; });
    logger.addLogEntry("stabilizer_zmpcc_comOffset", [this]() { return zmpccCoMOffset_; });
    logger.addLogEntry("stabilizer_zmpcc_comVel", [this]() { return zmpccCoMVel_; });
    logger.addLogEntry("stabilizer_zmpcc_error", [this]() { return zmpccError_; });
    logger.addLogEntry("stabilizer_zmpcc_leakRate", [this]() { return zmpccIntegrator_.rate(); });
  }

  void Stabilizer::addGUIElements(std::shared_ptr<mc_rtc::gui::StateBuilder> gui)
  {
    using namespace mc_rtc::gui;
    gui->addElement(
      {"Stabilizer", "Gains"},
      Button(
        "Disable",
        [this]() { disable(); }),
      Button(
        "Reconfigure",
        [this]() { reconfigure(); }),
      ArrayInput(
        "Foot admittance",
        {"CoPx", "CoPy", "DFz"},
        [this]() -> Eigen::Vector3d
        {
          return {copAdmittance_.x(), copAdmittance_.y(), dfzAdmittance_};
        },
        [this](const Eigen::Vector3d & a)
        {
          copAdmittance_.x() = clamp(a(0), 0., MAX_COP_ADMITTANCE);
          copAdmittance_.y() = clamp(a(1), 0., MAX_COP_ADMITTANCE);
          dfzAdmittance_ = clamp(a(2), 0., MAX_DFZ_ADMITTANCE);
        }),
      ArrayInput(
        "DCM feedback",
        {"proportional", "integral"},
        [this]() -> Eigen::Vector2d { return {dcmGain_, dcmIntegralGain_}; },
        [this](const Eigen::Vector2d & gains)
        {
          dcmGain_ = clamp(gains(0), MIN_DCM_P_GAIN, MAX_DCM_P_GAIN);
          dcmIntegralGain_ = clamp(gains(1), 0., MAX_DCM_I_GAIN);
        }),
      ArrayInput(
        "Vertical drift control",
        {"frequency", "stiffness", "damping"},
        [this]() -> Eigen::Vector3d { return {vdcFrequency_, vdcStiffness_, vdcDamping_}; },
        [this](const Eigen::Vector3d & v)
        {
          vdcFrequency_ = clamp(v(0), 0., 10.);
          vdcStiffness_ = clamp(v(1), 0., 1e4);
          vdcDamping_ = clamp(v(2), 0., 100.);
        }),
      ArrayInput(
        "CoM admittance",
        {"Ax", "Ay", "Az"},
        [this]() { return comAdmittance_; },
        [this](const Eigen::Vector3d & a)
        {
          comAdmittance_.x() = clamp(a.x(), 0., MAX_COM_XY_ADMITTANCE);
          comAdmittance_.y() = clamp(a.y(), 0., MAX_COM_XY_ADMITTANCE);
          comAdmittance_.z() = clamp(a.z(), 0., MAX_COM_Z_ADMITTANCE);
        }));
    gui->addElement(
      {"Stabilizer", "Integrators"},
      Button(
        "Reset DCM integrator",
        [this]() { dcmIntegrator_.setZero(); }),
      Button(
        "Reset ZMPCC integrator",
        [this]() { zmpccIntegrator_.setZero(); }),
      Button(
        "Reset Altitude integrator",
        [this]() { altccIntegrator_.setZero(); }),
      NumberInput(
        "DCM integrator T",
        [this]() { return dcmIntegrator_.timeConstant(); },
        [this](double T) { dcmIntegrator_.timeConstant(T); }),
      NumberInput(
        "ZMPCC leak rate [Hz]",
        [this]() { return zmpccIntegrator_.rate(); },
        [this](double T) { zmpccIntegrator_.rate(T); }),
      NumberInput(
        "Altitude CC leak rate [Hz]",
        [this]() { return altccIntegrator_.rate(); },
        [this](double T) { altccIntegrator_.rate(T); }));
    gui->addElement(
      {"Stabilizer", "Options"},
      NumberInput(
        "Mass [kg]",
        [this]() { return mass_; },
        [this](double mass) { mass_ = clamp(mass, 30., 45.); }),
      ComboInput(
        "Template model",
        {TEMPLATE_MODEL_LABELS[0], TEMPLATE_MODEL_LABELS[1]},
        [this]() { return templateModelToString(model_); },
        [this](const std::string & model) { model_ = templateModelFromString(model); }),
      Checkbox(
        "Use ZMPCC only in double support?",
        [this]() { return zmpccOnlyDS_; },
        [this]() { zmpccOnlyDS_ = !zmpccOnlyDS_; }));
    gui->addElement(
      {"Stabilizer", "Status"},
      Label(
        "Contact state",
        [this]()
        {
          switch (contactState_)
          {
            case ContactState::DoubleSupport:
              return "DoubleSupport";
            case ContactState::LeftFoot:
              return "LeftFoot";
            case ContactState::RightFoot:
            default:
              return "RightFoot";
          }
        }),
      ArrayLabel("DCM error [mm]",
        {"x", "y", "z"},
        [this]() { return roundVec(dcmError_ * 1000.); }),
      ArrayLabel("DCM average error [mm]",
        {"x", "y", "z"},
        [this]() { return roundVec(dcmAverageError_ * 1000.); }),
      ArrayLabel("CoM offset [mm]",
        {"x", "y", "z"},
        [this]() { return roundVec(comOffset_ * 1000.); }),
      ArrayLabel("Contact wrench error",
        {"ZMPx [cm]", "ZMPy [cm]", "lambda [Hz^2]"},
        [this]() { return roundVec({zmpccError_.x() * 100., zmpccError_.y() * 100., distribLambda_ - measuredLambda_}, /* fact = */ 10.); }),
      Label("Foot height diff [mm]",
        [this]() { return std::round(vfcZCtrl_ * 1000.); }));
  }

  void Stabilizer::disable()
  {
    comAdmittance_.setZero();
    copAdmittance_.setZero();
    dcmGain_ = 0.;
    dcmIntegralGain_ = 0.;
    dfzAdmittance_ = 0.;
    vdcFrequency_ = 0.;
    vdcStiffness_ = 0.;
  }

  void Stabilizer::configure(const mc_rtc::Configuration & config)
  {
    config_ = config;
    reconfigure();
  }

  void Stabilizer::reconfigure()
  {
    fdqpWeights_.configure(config_("fdqp_weights"));
    if (config_.has("admittance"))
    {
      auto admittance = config_("admittance");
      comAdmittance_ = admittance("com");
      copAdmittance_ = admittance("cop");
      dfzAdmittance_ = admittance("dfz");
    }
    if (config_.has("dcm_feedback"))
    {
      auto dcmConfig = config_("dcm_feedback");
      dcmGain_ = dcmConfig("gain");
      dcmIntegralGain_ = dcmConfig("integral_gain");
      dcmIntegrator_.timeConstant(dcmConfig("integrator_time_constant"));
    }
    if (config_.has("tasks"))
    {
      auto tasks = config_("tasks");
      if (tasks.has("com"))
      {
        tasks("com")("active_joints", comActiveJoints_);
        tasks("com")("stiffness", comStiffness_);
        tasks("com")("weight", comWeight_);
      }
      if (tasks.has("contact"))
      {
        double d = tasks("contact")("damping");
        double k = tasks("contact")("stiffness");
        contactDamping_ = sva::MotionVecd({d, d, d}, {d, d, d});
        contactStiffness_ = sva::MotionVecd({k, k, k}, {k, k, k});
        tasks("contact")("stiffness", contactStiffness_);
        tasks("contact")("weight", contactWeight_);
      }
      if (tasks.has("swing_foot"))
      {
        tasks("swing_foot")("stiffness", swingFootStiffness_);
        tasks("swing_foot")("weight", swingFootWeight_);
      }
    }
    if (config_.has("vdc"))
    {
      auto vdc = config_("vdc");
      vdcDamping_ = vdc("damping");
      vdcFrequency_ = vdc("frequency");
      vdcStiffness_ = vdc("stiffness");
    }
    if (config_.has("altcc"))
    {
      altccIntegrator_.rate(config_("altcc")("integrator_leak_rate"));
    }
    if (config_.has("zmpcc"))
    {
      zmpccIntegrator_.rate(config_("zmpcc")("integrator_leak_rate"));
    }
  }

  void Stabilizer::reset(const mc_rbdyn::Robots & robots)
  {
    unsigned robotIndex = robots.robotIndex();

    comTask.reset(new mc_tasks::CoMTask(robots, robotIndex));
    comTask->selectActiveJoints(comActiveJoints_);
    comTask->setGains(comStiffness_, 2 * comStiffness_.cwiseSqrt());
    comTask->weight(comWeight_);

    leftFootTask.reset(new mc_tasks::force::CoPTask("LeftFootCenter", robots, robotIndex));
    rightFootTask.reset(new mc_tasks::force::CoPTask("RightFootCenter", robots, robotIndex));
    leftFootTask->maxAngularVel({MAX_FDC_RX_VEL, MAX_FDC_RY_VEL, MAX_FDC_RZ_VEL});
    rightFootTask->maxAngularVel({MAX_FDC_RX_VEL, MAX_FDC_RY_VEL, MAX_FDC_RZ_VEL});
    setContact(leftFootTask, leftFootTask->surfacePose());
    setContact(rightFootTask, rightFootTask->surfacePose());

    dcmIntegrator_.setZero();
    dcmIntegrator_.saturation(MAX_AVERAGE_DCM_ERROR);
    altccIntegrator_.setZero();
    altccIntegrator_.saturation(MAX_ALTCC_COM_OFFSET);
    zmpccIntegrator_.setZero();
    zmpccIntegrator_.saturation(MAX_ZMPCC_COM_OFFSET);

    Eigen::Vector3d staticForce = -mass_ * world::gravity;

    altccCoMAccel_.setZero();
    altccCoMOffset_.setZero();
    altccCoMVel_.setZero();
    altccError_.setZero();
    comOffset_.setZero();
    dcmAverageError_.setZero();
    dcmError_.setZero();
    distribWrench_ = {pendulum_.com().cross(staticForce), staticForce};
    logMeasuredDFz_ = 0.;
    logMeasuredSTz_ = 0.;
    logTargetDFz_ = 0.;
    logTargetSTz_ = 0.;
    zmpccCoMAccel_.setZero();
    zmpccCoMOffset_.setZero();
    zmpccCoMVel_.setZero();
    zmpccError_.setZero();
  }

  void Stabilizer::checkGains()
  {
    clampInPlace(comAdmittance_.x(), 0., MAX_COM_XY_ADMITTANCE, "CoM x-admittance");
    clampInPlace(comAdmittance_.y(), 0., MAX_COM_XY_ADMITTANCE, "CoM y-admittance");
    clampInPlace(comAdmittance_.z(), 0., MAX_COM_Z_ADMITTANCE, "CoM z-admittance");
    clampInPlace(copAdmittance_.x(), 0., MAX_COP_ADMITTANCE, "CoP x-admittance");
    clampInPlace(copAdmittance_.y(), 0., MAX_COP_ADMITTANCE, "CoP y-admittance");
    clampInPlace(dcmGain_, MIN_DCM_P_GAIN, MAX_DCM_P_GAIN, "DCM x-gain");
    clampInPlace(dcmIntegralGain_, 0., MAX_DCM_I_GAIN, "DCM integral x-gain");
    clampInPlace(dfzAdmittance_, 0., MAX_DFZ_ADMITTANCE, "DFz admittance");
  }

  void Stabilizer::addTasks(mc_solver::QPSolver & solver)
  {
    solver.addTask(comTask);
    solver.addTask(leftFootTask);
    solver.addTask(rightFootTask);
  }

  void Stabilizer::removeTasks(mc_solver::QPSolver & solver)
  {
    solver.removeTask(comTask);
    solver.removeTask(leftFootTask);
    solver.removeTask(rightFootTask);
  }

  void Stabilizer::setContact(std::shared_ptr<mc_tasks::force::CoPTask> footTask, const Contact & contact)
  {
    footTask->reset();
    footTask->admittance(contactAdmittance());
    footTask->setGains(contactStiffness_, contactDamping_);
    footTask->targetPose(contact.pose);
    footTask->weight(contactWeight_);
    if (footTask->surface() == "LeftFootCenter")
    {
      leftFootContact = contact;
    }
    else if (footTask->surface() == "RightFootCenter")
    {
      rightFootContact = contact;
    }
    else
    {
      mc_rtc::log::warning("Unknown foot surface: {}", footTask->surface());
    }
  }

  void Stabilizer::setSwingFoot(std::shared_ptr<mc_tasks::force::CoPTask> footTask)
  {
    footTask->reset();
    footTask->stiffness(swingFootStiffness_); // sets damping as well
    footTask->weight(swingFootWeight_);
  }

  bool Stabilizer::detectTouchdown(const std::shared_ptr<mc_tasks::force::CoPTask> footTask, const Contact & contact)
  {
    const sva::PTransformd X_0_s = footTask->surfacePose();
    const sva::PTransformd & X_0_c = contact.pose;
    sva::PTransformd X_c_s = X_0_s * X_0_c.inv();
    double xDist = std::abs(X_c_s.translation().x());
    double yDist = std::abs(X_c_s.translation().y());
    double zDist = std::abs(X_c_s.translation().z());
    double pressure = footTask->measuredWrench().force().z();
    return (xDist < 0.03 && yDist < 0.03 && zDist < 0.03 && pressure > 50.);
  }

  void Stabilizer::seekTouchdown(std::shared_ptr<mc_tasks::force::CoPTask> footTask)
  {
    constexpr double MAX_VEL = 0.01; // [m] / [s]
    constexpr double TOUCHDOWN_PRESSURE = 50.;  // [N]
    constexpr double DESIRED_AFZ = MAX_VEL / TOUCHDOWN_PRESSURE;
    if (footTask->measuredWrench().force().z() < TOUCHDOWN_PRESSURE)
    {
      auto a = footTask->admittance();
      double AFz = clamp(DESIRED_AFZ, 0., 1e-2, "Contact seeking admittance");
      footTask->admittance({a.couple(), {a.force().x(), a.force().y(), AFz}});
      footTask->targetForce({0., 0., TOUCHDOWN_PRESSURE});
    }
  }

  void Stabilizer::updateSupportFootGains()
  {
    sva::MotionVecd vdcContactStiffness = {
      contactStiffness_.angular(),
      {vdcStiffness_, vdcStiffness_, vdcStiffness_}};
    switch (contactState_)
    {
      case ContactState::DoubleSupport:
        leftFootTask->admittance(contactAdmittance());
        leftFootTask->setGains(contactStiffness_, contactDamping_);
        rightFootTask->admittance(contactAdmittance());
        rightFootTask->setGains(contactStiffness_, contactDamping_);
        break;
      case ContactState::LeftFoot:
        leftFootTask->admittance(contactAdmittance());
        leftFootTask->setGains(vdcContactStiffness, contactDamping_);
        break;
      case ContactState::RightFoot:
        rightFootTask->admittance(contactAdmittance());
        rightFootTask->setGains(vdcContactStiffness, contactDamping_);
        break;
    }
  }

  void Stabilizer::checkInTheAir()
  {
    double LFz = leftFootTask->measuredWrench().force().z();
    double RFz = rightFootTask->measuredWrench().force().z();
    inTheAir_ = (LFz < MIN_DS_PRESSURE && RFz < MIN_DS_PRESSURE);
  }

  void Stabilizer::updateZMPFrame()
  {
    const sva::PTransformd & X_0_lc = leftFootContact.pose;
    const sva::PTransformd & X_0_rc = rightFootContact.pose;
    if (contactState_ == ContactState::DoubleSupport)
    {
      zmpFrame_ = sva::interpolate(X_0_lc, X_0_rc, 0.5);
      double xmin = std::min(leftFootContact.xmin(), rightFootContact.xmin());
      double xmax = std::max(leftFootContact.xmax(), rightFootContact.xmax());
      double ymin = std::min(leftFootContact.ymin(), rightFootContact.ymin());
      double ymax = std::max(leftFootContact.ymax(), rightFootContact.ymax());
      Eigen::Matrix<double, 4, 2> hrepMat;
      Eigen::Matrix<double, 4, 1> hrepVec;
      hrepMat <<
        +1, 0,
        -1, 0,
        0, +1,
        0, -1;
      hrepVec <<
        xmax - zmpFrame_.translation().x(),
        zmpFrame_.translation().x() - xmin,
        ymax - zmpFrame_.translation().y(),
        zmpFrame_.translation().y() - ymin;
      zmpArea_ = Eigen::HrepXd(hrepMat, hrepVec);
      zmpPolygon_.clear();
      zmpPolygon_.push_back(Eigen::Vector3d{xmax, ymax, zmpFrame_.translation().z()});
      zmpPolygon_.push_back(Eigen::Vector3d{xmax, ymin, zmpFrame_.translation().z()});
      zmpPolygon_.push_back(Eigen::Vector3d{xmin, ymin, zmpFrame_.translation().z()});
      zmpPolygon_.push_back(Eigen::Vector3d{xmin, ymax, zmpFrame_.translation().z()});
    }
    else if (contactState_ == ContactState::LeftFoot)
    {
      zmpFrame_ = X_0_lc;
      zmpArea_ = leftFootContact.localHrep();
      zmpPolygon_.clear();
      zmpPolygon_.push_back(leftFootContact.vertex0());
      zmpPolygon_.push_back(leftFootContact.vertex1());
      zmpPolygon_.push_back(leftFootContact.vertex2());
      zmpPolygon_.push_back(leftFootContact.vertex3());
    }
    else // (contactState_ == ContactState::RightFoot)
    {
      zmpFrame_ = X_0_rc;
      zmpArea_ = rightFootContact.localHrep();
      zmpPolygon_.clear();
      zmpPolygon_.push_back(rightFootContact.vertex0());
      zmpPolygon_.push_back(rightFootContact.vertex1());
      zmpPolygon_.push_back(rightFootContact.vertex2());
      zmpPolygon_.push_back(rightFootContact.vertex3());
    }
    measuredZMP_ = computeZMP(measuredWrench_);
  }

  Eigen::Vector3d Stabilizer::computeZMP(const sva::ForceVecd & wrench) const
  {
    Eigen::Vector3d n = zmpFrame_.rotation().row(2);
    Eigen::Vector3d p = zmpFrame_.translation();
    const Eigen::Vector3d & force = wrench.force();
    double pressure = n.dot(force);
    if (pressure < 1.)
    {
      double omega2 = std::pow(pendulum_.omega(), 2);
      return measuredCoM_ + world::gravity / omega2; // default for logging
    }
    const Eigen::Vector3d & moment_0 = wrench.couple();
    Eigen::Vector3d moment_p = moment_0 - p.cross(force);
    return p + n.cross(moment_p) / pressure;
  }

  void Stabilizer::run()
  {
    using namespace std::chrono;
    auto startTime = high_resolution_clock::now();

    checkGains();
    checkInTheAir();
    updateSupportFootGains();
    updateZMPFrame();

    sva::ForceVecd desiredWrench = computeDesiredWrench();
    distributeWrench(desiredWrench);
    updateCoMAdmittanceControl();
    updateFootForceDifferenceControl();

    auto endTime = high_resolution_clock::now();
    runTime_ = 1000. * duration_cast<duration<double>>(endTime - startTime).count();
  }

  sva::ForceVecd Stabilizer::computeDesiredWrench()
  {
    sva::ForceVecd desiredWrench;
    if (model_ == TemplateModel::LinearInvertedPendulum)
    {
      desiredWrench = computeLIPDesiredWrench();
    }
    else // (model_ == TemplateModel::VariableHeightInvertedPendulum)
    {
      using namespace std::chrono;
      auto startTime = high_resolution_clock::now();
      desiredWrench = computeVHIPDesiredWrench();
      auto endTime = high_resolution_clock::now();
      vhipRunTime_ = 1000. * duration_cast<duration<double>>(endTime - startTime).count();
    }
    return desiredWrench;
  }

  sva::ForceVecd Stabilizer::computeLIPDesiredWrench()
  {
    double omega = pendulum_.omega();
    double omega2 = omega * omega;
    Eigen::Vector3d comError = pendulum_.com() - measuredCoM_;
    Eigen::Vector3d comdError = pendulum_.comd() - measuredCoMd_;
    dcmError_ = comError + comdError / omega;
    dcmError_.z() = 0.;

    if (!inTheAir_) // don't accumulate error if robot is in the air
    {
      dcmIntegrator_.append(dcmError_);
      dcmAverageError_ = dcmIntegrator_.eval();
    }

    Eigen::Vector3d desiredCoMAccel = pendulum_.comdd();
    desiredCoMAccel += dcmGain_ * omega2 * dcmError_ + omega * comdError;
    desiredCoMAccel += dcmIntegralGain_ * omega2 * dcmAverageError_;
    Eigen::Vector3d desiredForce = mass_ * (desiredCoMAccel - world::gravity);
    return {pendulum_.com().cross(desiredForce), desiredForce};
  }

  sva::ForceVecd Stabilizer::computeVHIPDesiredWrench()
  {
    double vrpGain = dcmGain_ + 1.;
    double refOmega = pendulum_.omega();
    double refLambda = refOmega * refOmega;
    Eigen::Vector3d comError = measuredCoM_ - pendulum_.com();
    Eigen::Vector3d comdError = measuredCoMd_ - pendulum_.comd();
    Eigen::Vector3d refCoM = pendulum_.com();
    Eigen::Vector3d refDCM = pendulum_.com() + pendulum_.comd() / refOmega;
    Eigen::Vector3d refVRP = pendulum_.zmp() - world::gravity / refLambda;
    const Eigen::Vector3d & refZMP = pendulum_.zmp();

    // TODO: move to configuration file
    constexpr double MAX_DCM_HEIGHT = 0.9; // [m]
    constexpr double MIN_DCM_HEIGHT = 0.5; // [m]
    constexpr double MAX_FORCE = 500.; // [N]
    constexpr double MIN_FORCE = 1.; // [N]

    double measuredHeight = measuredCoM_.z() - zmpFrame_.translation().z();
    lambdaMax_ = MAX_FORCE / (mass_ * measuredHeight);
    lambdaMin_ = MIN_FORCE / (mass_ * measuredHeight);
    double omegaMax = std::sqrt(lambdaMax_);
    double omegaMin = std::sqrt(lambdaMin_);

    constexpr unsigned NB_VARIABLES = 3 + 1 + 2 + 1 + 3;
    Eigen::MatrixXd A;
    Eigen::VectorXd b;
    A.setZero(3, NB_VARIABLES);
    A <<
      0., 0., 0., 0., 0., 0., 0., 1., 0., 0.,
      0., 0., 0., 0., 0., 0., 0., 0., 1., 0.,
      0., 0., 0., 0., 0., 0., 0., 0., 0., std::sqrt(1e-3),
    b.setZero(3);

    unsigned nbConstraints = 3 + 3 + 1 + zmpArea_.first.rows() + 1;
    Eigen::VectorXd bl, bu;
    bl.setConstant(NB_VARIABLES + nbConstraints, -1e5);
    bu.setConstant(NB_VARIABLES + nbConstraints, +1e5);
    auto blVar = bl.head<NB_VARIABLES>();
    auto buVar = bu.head<NB_VARIABLES>();
    blVar <<
      -1., // 0: Delta xi_x [m]
      -1., // 1: Delta xi_y [m]
      -1., // 2: Delta xi_z [m]
      omegaMin - refOmega, // refOmega + Delta_omega >= omegaMin
      -1., // 4: Delta zmp_x [m]
      -1., // 5: Delta zmp_y [m]
      lambdaMin_ - refLambda, // refLambda + Delta_lambda >= lambdaMin_
      -1., // 7: Delta sigma_x [m]
      -1., // 8: Delta sigma_y [m]
      -1.; // 9: Delta sigma_z [m]
    buVar <<
      +1., // 0: Delta xi_x [m]
      +1., // 1: Delta xi_y [m]
      +1., // 2: Delta xi_z [m]
      omegaMax - refOmega, // refOmega + Delta_omega <= omegaMax
      +1., // 4: Delta zmp_x [m]
      +1., // 5: Delta zmp_y [m]
      lambdaMax_ - refLambda, // refLambda + Delta_lambda <= lambdaMax_
      +1., // 7: Delta sigma_x [m]
      +1., // 8: Delta sigma_y [m]
      +1.; // 9: Delta sigma_z [m]

    auto blCons = bl.tail(nbConstraints);
    auto buCons = bu.tail(nbConstraints);
    Eigen::MatrixXd C;
    C.setZero(nbConstraints, NB_VARIABLES);

    const Eigen::Matrix3d R_zmpFrame_0 = zmpFrame_.rotation().transpose();
    const Eigen::Matrix<double, 3, 2> R_Delta_zmp = R_zmpFrame_0.block<3, 2>(0, 0);

    unsigned curRow = 0;
    unsigned curHeight = 3;
    C.block(curRow, 0, curHeight, 3) = -vrpGain * Eigen::Matrix3d::Identity();
    C.block(curRow, 3, curHeight, 1) = (refDCM - refVRP) / refOmega;
    C.block(curRow, 4, curHeight, 2) = R_Delta_zmp;
    C.block(curRow, 6, curHeight, 1) = (refZMP - refDCM) / refLambda;
    C.block(curRow, 7, curHeight, 3) = Eigen::Matrix3d::Identity();
    blCons.segment(curRow, curHeight).setZero();
    buCons.segment(curRow, curHeight).setZero();

    curRow += curHeight;
    curHeight = 3;
    C.block(curRow, 0, curHeight, 3) = Eigen::Matrix3d::Identity();
    C.block(curRow, 3, curHeight, 1) = measuredCoMd_ / (refOmega * refOmega);
    Eigen::Vector3d constantOmegaDCM = comError + comdError / refOmega;
    blCons.segment(curRow, curHeight) = constantOmegaDCM;
    buCons.segment(curRow, curHeight) = constantOmegaDCM;

    curRow += curHeight;
    curHeight = 1;
    C(curRow, 3) = refOmega * (1 + vrpGain);
    C(curRow, 6) = -1.;
    blCons[curRow] = 0.;
    buCons[curRow] = 0.;

    Eigen::Vector3d refFrameZMP = zmpFrame_.rotation() * (pendulum_.zmp() - zmpFrame_.translation());
    if (std::abs(refFrameZMP.z()) > 1e-3)
    {
      mc_rtc::log::warning("Reference ZMP does not belong to the ZMP frame");
    }

    curRow += curHeight;
    curHeight = zmpArea_.first.rows();
    C.block(curRow, 4, curHeight, 2) = zmpArea_.first;
    blCons.segment(curRow, curHeight).setConstant(-1e5);
    buCons.segment(curRow, curHeight) = zmpArea_.second - zmpArea_.first * refFrameZMP;

    curRow += curHeight;
    curHeight = 1;
    double dcmDamping = 0.5;
    double alpha = (1 + dcmDamping) * refLambda * dt_ / refOmega;
    C(curRow, 2) = 1 + alpha * (1 - vrpGain);
    C(curRow, 9) = alpha;
    blCons[curRow] = MIN_DCM_HEIGHT - refDCM.z();
    buCons[curRow] = MAX_DCM_HEIGHT - refDCM.z();

    curRow += curHeight;
    if (curRow != nbConstraints)
    {
      mc_rtc::log::error("Invalid number of constraints in VHIP feedback QP");
    }

    bool solverSuccess = leastSquares_.solve(A, b, C, bl, bu);
    Eigen::VectorXd Delta_x = leastSquares_.result();
    if (!solverSuccess)
    {
      mc_rtc::log::error("VHIP feedback QP failed to run");
      leastSquares_.print_inform();
      return computeLIPDesiredWrench();
    }

    double Delta_omega = Delta_x[3];
    double Delta_lambda = Delta_x[6];
    Eigen::Vector2d Delta_zmp = Delta_x.segment<2>(4);

    vhipOmega_ = refOmega + Delta_omega;
    vhipLambda_ = refLambda + Delta_lambda;
    vhipDCM_ = measuredCoM_ + measuredCoMd_ / vhipOmega_;
    vhipZMP_ = refZMP + R_Delta_zmp * Delta_zmp;
    Eigen::Vector3d desiredForce = mass_ * vhipLambda_ * (refCoM - vhipZMP_);
    return {vhipZMP_.cross(desiredForce), desiredForce};
  }

  void Stabilizer::distributeWrench(const sva::ForceVecd & desiredWrench)
  {
    using namespace std::chrono;
    auto startTime = high_resolution_clock::now();

    switch (contactState_)
    {
      case ContactState::DoubleSupport:
        distributeWrenchDS(desiredWrench);
        break;
      case ContactState::LeftFoot:
        distributeWrenchSS(desiredWrench, leftFootTask);
        rightFootTask->setZeroTargetWrench();
        break;
      case ContactState::RightFoot:
        distributeWrenchSS(desiredWrench, rightFootTask);
        leftFootTask->setZeroTargetWrench();
        break;
    }

    auto endTime = high_resolution_clock::now();
    fdqpRunTime_ = 1000. * duration_cast<duration<double>>(endTime - startTime).count();
  }

  void Stabilizer::distributeWrenchDS(const sva::ForceVecd & desiredWrench)
  {
    // Variables
    // ---------
    // x = [w_l_0 w_r_0] where
    // w_l_0: spatial force vector of left foot contact in inertial frame
    // w_r_0: spatial force vector of right foot contact in inertial frame
    //
    // Objective
    // ---------
    // Weighted minimization of the following tasks:
    // w_l_0 + w_r_0 == desiredWrench  -- realize desired contact wrench
    // w_l_lankle == 0 -- minimize left foot ankle torque (anisotropic weight)
    // w_r_rankle == 0 -- minimize right foot ankle torque (anisotropic weight)
    // (1 - lfr) * w_l_lc.z() == lfr * w_r_rc.z()
    //
    // Constraints
    // -----------
    // CWC X_0_lc* w_l_0 <= 0  -- left foot wrench within contact wrench cone
    // CWC X_0_rc* w_r_0 <= 0  -- right foot wrench within contact wrench cone
    // (X_0_lc* w_l_0).z() > minPressure  -- minimum left foot contact pressure
    // (X_0_rc* w_r_0).z() > minPressure  -- minimum right foot contact pressure

    const sva::PTransformd & X_0_lc = leftFootContact.pose;
    const sva::PTransformd & X_0_rc = rightFootContact.pose;
    sva::PTransformd X_0_lankle = leftFootContact.anklePose();
    sva::PTransformd X_0_rankle = rightFootContact.anklePose();

    constexpr unsigned NB_VAR = 6 + 6;
    constexpr unsigned COST_DIM = 6 + NB_VAR + 1;
    Eigen::MatrixXd A;
    Eigen::VectorXd b;
    A.setZero(COST_DIM, NB_VAR);
    b.setZero(COST_DIM);

    // |w_l_0 + w_r_0 - desiredWrench|^2
    auto A_net = A.block<6, 12>(0, 0);
    auto b_net = b.segment<6>(0);
    A_net.block<6, 6>(0, 0) = Eigen::Matrix6d::Identity();
    A_net.block<6, 6>(0, 6) = Eigen::Matrix6d::Identity();
    b_net = desiredWrench.vector();

    // |ankle torques|^2
    auto A_lankle = A.block<6, 6>(6, 0);
    auto A_rankle = A.block<6, 6>(12, 6);
    // anisotropic weights:  taux, tauy, tauz,   fx,   fy,   fz;
    A_lankle.diagonal() <<     1.,   1., 1e-4, 1e-3, 1e-3, 1e-4;
    A_rankle.diagonal() <<     1.,   1., 1e-4, 1e-3, 1e-3, 1e-4;
    A_lankle *= X_0_lankle.dualMatrix();
    A_rankle *= X_0_rankle.dualMatrix();

    // |(1 - lfr) * w_l_lc.force().z() - lfr * w_r_rc.force().z()|^2
    double lfr = leftFootRatio_;
    auto A_pressure = A.block<1, 12>(18, 0);
    A_pressure.block<1, 6>(0, 0) = (1 - lfr) * X_0_lc.dualMatrix().bottomRows<1>();
    A_pressure.block<1, 6>(0, 6) = -lfr * X_0_rc.dualMatrix().bottomRows<1>();

    // Apply weights
    A_net *= fdqpWeights_.netWrenchSqrt;
    b_net *= fdqpWeights_.netWrenchSqrt;
    A_lankle *= fdqpWeights_.ankleTorqueSqrt;
    A_rankle *= fdqpWeights_.ankleTorqueSqrt;
    // b_lankle = 0
    // b_rankle = 0
    A_pressure *= fdqpWeights_.pressureSqrt;
    // b_pressure = 0

    constexpr unsigned CONS_DIM = 16 + 16 + 2;
    Eigen::Matrix<double, CONS_DIM , NB_VAR> C;
    Eigen::VectorXd bl, bu;
    C.setZero(CONS_DIM, NB_VAR);
    bl.setConstant(NB_VAR + CONS_DIM, -1e5);
    bu.setConstant(NB_VAR + CONS_DIM, +1e5);
    auto blCons = bl.tail<CONS_DIM>();
    auto buCons = bu.tail<CONS_DIM>();
    // CWC * w_l_lc <= 0
    C.block<16, 6>(0, 0) = wrenchFaceMatrix_ * X_0_lc.dualMatrix();
    buCons.segment<16>(0).setZero();
    // CWC * w_r_rc <= 0
    C.block<16, 6>(16, 6) = wrenchFaceMatrix_ * X_0_rc.dualMatrix();
    buCons.segment<16>(16).setZero();
    // w_l_lc.force().z() >= MIN_DS_PRESSURE
    // w_r_rc.force().z() >= MIN_DS_PRESSURE
    C.block<1, 6>(32, 0) = X_0_lc.dualMatrix().bottomRows<1>();
    C.block<1, 6>(33, 6) = X_0_rc.dualMatrix().bottomRows<1>();
    blCons.segment<2>(32).setConstant(MIN_DS_PRESSURE);
    buCons.segment<2>(32).setConstant(+1e5);

    //Eigen::MatrixXd A0 = A; // A is modified by solve()
    //Eigen::VectorXd b0 = b; // b is modified by solve()
    bool solverSuccess = leastSquares_.solve(A, b, C, bl, bu);
    Eigen::VectorXd x = leastSquares_.result();
    if (!solverSuccess)
    {
      mc_rtc::log::error("DS force distribution QP failed to run");
      return;
    }

    sva::ForceVecd w_l_0(x.segment<3>(0), x.segment<3>(3));
    sva::ForceVecd w_r_0(x.segment<3>(6), x.segment<3>(9));
    distribWrench_ = w_l_0 + w_r_0;

    sva::ForceVecd w_l_lc = X_0_lc.dualMul(w_l_0);
    sva::ForceVecd w_r_rc = X_0_rc.dualMul(w_r_0);
    Eigen::Vector2d leftCoP = (e_z.cross(w_l_lc.couple()) / w_l_lc.force()(2)).head<2>();
    Eigen::Vector2d rightCoP = (e_z.cross(w_r_rc.couple()) / w_r_rc.force()(2)).head<2>();
    leftFootTask->targetCoP(leftCoP);
    leftFootTask->targetForce(w_l_lc.force());
    rightFootTask->targetCoP(rightCoP);
    rightFootTask->targetForce(w_r_rc.force());
  }

  void Stabilizer::distributeWrenchSS(const sva::ForceVecd & desiredWrench, std::shared_ptr<mc_tasks::force::CoPTask> & footTask)
  {
    constexpr unsigned NB_CONS = 16;
    constexpr unsigned NB_VAR = 6;

    // Variables
    // ---------
    // x = [w_0] where
    // w_0: spatial force vector of foot contact in inertial frame
    //
    // Objective
    // ---------
    // weighted minimization of |w_c - X_0_c* desiredWrench|^2
    //
    // Constraints
    // -----------
    // F X_0_c* w_0 <= 0    -- contact stability

    const sva::PTransformd & X_0_c = footTask->targetPose();

    Eigen::Matrix6d A = Eigen::Matrix6d::Identity();
    Eigen::Vector6d b = desiredWrench.vector();

    Eigen::MatrixXd C = wrenchFaceMatrix_ * X_0_c.dualMatrix();
    Eigen::VectorXd bl, bu;
    bl.setConstant(NB_VAR + NB_CONS, -1e5);
    bu.setConstant(NB_VAR + NB_CONS, +1e5);
    bu.tail<NB_CONS>().setZero();

    //Eigen::MatrixXd A0 = A; // A is modified by solve()
    //Eigen::VectorXd b0 = b; // b is modified by solve()
    leastSquares_.solve(A, b, C, bl, bu);
    Eigen::VectorXd x = leastSquares_.result();
    if (leastSquares_.inform() != Eigen::lssol::eStatus::STRONG_MINIMUM)
    {
      mc_rtc::log::error("SS force distribution QP failed to run");
      return;
    }

    sva::ForceVecd w_0(x.head<3>(), x.tail<3>());
    sva::ForceVecd w_c = X_0_c.dualMul(w_0);
    Eigen::Vector2d cop = (e_z.cross(w_c.couple()) / w_c.force()(2)).head<2>();
    footTask->targetCoP(cop);
    footTask->targetForce(w_c.force());
    distribWrench_ = w_0;
  }

  void Stabilizer::updateCoMZMPCC()
  {
    auto distribZMP = computeZMP(distribWrench_);
    zmpccError_ = distribZMP - measuredZMP_;
    if (zmpccOnlyDS_ && contactState_ != ContactState::DoubleSupport)
    {
      zmpccIntegrator_.add(Eigen::Vector3d::Zero(), dt_); // leak to zero
      zmpccCoMAccel_.setZero();
      zmpccCoMVel_.setZero();
    }
    else
    {
      const Eigen::Matrix3d & R_0_c = zmpFrame_.rotation();
      const Eigen::Transpose<const Eigen::Matrix3d> R_c_0 = R_0_c.transpose();
      Eigen::Vector3d comAdmittanceZMP = {comAdmittance_.x(), comAdmittance_.y(), 0.};
      Eigen::Vector3d newVel = -R_c_0 * comAdmittanceZMP.cwiseProduct(R_0_c * zmpccError_);
      Eigen::Vector3d newAccel = (newVel - zmpccCoMVel_) / dt_;
      zmpccIntegrator_.add(newVel, dt_);
      zmpccCoMAccel_ = newAccel;
      zmpccCoMVel_ = newVel;
    }
    zmpccCoMOffset_ = zmpccIntegrator_.eval();
  }

  void Stabilizer::updateCoMAltitude()
  {
    double measuredHeight = measuredCoM_.z() - zmpFrame_.translation().z();
    double pendulumHeight = pendulum_.com().z() - zmpFrame_.translation().z();
    distribLambda_ = distribWrench_.force().z() / (mass_ * pendulumHeight);
    measuredLambda_ = measuredWrench_.force().z() / (mass_ * measuredHeight);
    if (model_ == TemplateModel::LinearInvertedPendulum)
    {
      altccIntegrator_.add(Eigen::Vector3d::Zero(), dt_); // leak to zero
      altccCoMAccel_.setZero();
      altccCoMVel_.setZero();
    }
    else
    {
      const Eigen::Matrix3d & R_0_imu = controlRobot_.bodySensor().orientation().toRotationMatrix();
      const Eigen::Matrix3d & R_0_base = controlRobot_.posW().rotation();
      Eigen::Matrix3d R_base_imu = R_0_imu * R_0_base.transpose();
      Eigen::Vector3d worldVertical = R_base_imu.col(2);
      double zd = comAdmittance_.z() * (distribLambda_ - measuredLambda_);
      Eigen::Vector3d newVel = zd * worldVertical;
      Eigen::Vector3d newAccel = (newVel - altccCoMVel_) / dt_;
      altccIntegrator_.add(newVel, dt_);
      altccCoMAccel_ = newAccel;
      altccCoMVel_ = newVel;
    }
    altccCoMOffset_ = altccIntegrator_.eval();
  }

  void Stabilizer::updateCoMAdmittanceControl()
  {
    updateCoMZMPCC();
    updateCoMAltitude();

    comOffset_ = zmpccCoMOffset_ + altccCoMOffset_;
    auto comVelOffset = zmpccCoMVel_ + altccCoMVel_;
    auto comAccelOffset = zmpccCoMAccel_ + altccCoMAccel_;
    comTask->com(pendulum_.com() + comOffset_);
    comTask->refVel(pendulum_.comd() + comVelOffset);
    comTask->refAccel(pendulum_.comdd() + comAccelOffset);
  }

  void Stabilizer::updateFootForceDifferenceControl()
  {
    double LFz = leftFootTask->measuredWrench().force().z();
    double RFz = rightFootTask->measuredWrench().force().z();
    if (contactState_ == ContactState::DoubleSupport && !inTheAir_)
    {
      double LFz_d = leftFootTask->targetWrench().force().z();
      double RFz_d = rightFootTask->targetWrench().force().z();
      double dz_ctrl = dfzAdmittance_ * ((LFz_d - RFz_d) - (LFz - RFz));

      double LTz = leftFootTask->surfacePose().translation().z();
      double RTz = rightFootTask->surfacePose().translation().z();
      vfcZCtrl_ = RTz - LTz;
      dz_ctrl -= vdcDamping_ * vfcZCtrl_;

      double LTz_d = leftFootTask->targetPose().translation().z();
      double RTz_d = rightFootTask->targetPose().translation().z();
      double dz_pos = vdcFrequency_ * ((LTz_d + RTz_d) - (LTz + RTz));
      vdcZPos_ = RTz + LTz;

      sva::MotionVecd velF = {{0., 0., 0.}, {0., 0., dz_ctrl}};
      sva::MotionVecd velT = {{0., 0., 0.}, {0., 0., dz_pos}};
      leftFootTask->refVelB(0.5 * (velT - velF));
      rightFootTask->refVelB(0.5 * (velT + velF));

      logMeasuredDFz_ = LFz - RFz;
      logMeasuredSTz_ = LTz + RTz;
      logTargetDFz_ = LFz_d - RFz_d;
      logTargetSTz_ = LTz_d + RTz_d;
    }
    else
    {
      leftFootTask->refVelB({{0., 0., 0.}, {0., 0., 0.}});
      rightFootTask->refVelB({{0., 0., 0.}, {0., 0., 0.}});

      logMeasuredDFz_ = 0.;
      logMeasuredSTz_ = 0.;
      logTargetDFz_ = 0.;
      logTargetSTz_ = 0.;
      vdcZPos_ = 0.;
      vfcZCtrl_ = 0.;
    }
  }
}
