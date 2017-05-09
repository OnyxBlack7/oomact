#include <memory>

#include <gtest/gtest.h>
#include <glog/logging.h>

#include <sm/boost/null_deleter.hpp>
#include<eigen-checks/gtest.h>

#include <aslam/calibration/model/Model.h>
#include <aslam/calibration/model/sensors/MotionCaptureSensor.hpp>
#include <aslam/calibration/model/sensors/PoseSensor.hpp>
#include <aslam/calibration/model/PoseTrajectory.h>
#include <aslam/calibration/model/fragments/So3R3Trajectory.h>
#include <aslam/calibration/CalibratorI.hpp>
#include <aslam/calibration/model/FrameGraphModel.h>

#include "aslam/calibration/algo/MotionCaptureSource.hpp"

using namespace aslam::calibration;
class SimpleModelFrame : public Frame, public NamedMinimal {
  using NamedMinimal::NamedMinimal;
};
SimpleModelFrame world("world");
SimpleModelFrame body("body");

class MockMotionCaptureSource : public MotionCaptureSource {
 public:
  MockMotionCaptureSource(std::function<void(Timestamp start, Timestamp now, PoseStamped & p)> func) : func(func){}
  virtual ~MockMotionCaptureSource() = default;
  virtual std::vector<PoseStamped> getPoses(Timestamp from, Timestamp till) const override {
    Timestamp inc(1e-2);
    std::vector<PoseStamped> poses;
    for(auto t = from; t <= till + inc; t += inc){
      poses.resize(poses.size() + 1);
      poses.back().time = t;
      func(from, t, poses.back());
    }
    return poses;
  }

 private:
  std::function<void(Timestamp start, Timestamp now, PoseStamped & p)> func;
};


TEST(TestCalibration, testEstimatePoseSensorsInit) {
  auto vs = ValueStoreRef::fromString(
      "Gravity{used=false}"
      "a{frame=body,targetFrame=world,rotation/used=false,translation{used=true,x=0,y=5,z=0},delay/used=false}"
      "traj{frame=body,referenceFrame=world,McSensor=a,initWithPoseMeasurements=true,splines{knotsPerSecond=5,rotSplineOrder=4,rotFittingLambda=0.001,transSplineOrder=4,transFittingLambda=0.001}}"
    );

  FrameGraphModel m(vs, nullptr, {&world, &body});
  PoseSensor mcSensorA(m, "a", vs);
  PoseTrajectory traj(m, "traj", vs);
  m.addModulesAndInit(mcSensorA, traj);

  EXPECT_EQ(1, m.getCalibrationVariables().size());
  EXPECT_DOUBLE_EQ(5.0, mcSensorA.getTranslationToParent()[1]);

  MockMotionCaptureSource mmcs([](Timestamp start, Timestamp now, MotionCaptureSource::PoseStamped & p){
    p.q = sm::kinematics::quatIdentity();
    p.p = Eigen::Vector3d::UnitX() * (now - start);
  });

  auto vsCalib = ValueStoreRef::fromString(
      "verbose=true\n"
      "acceptConstantErrorTerms=true\n"
      "estimator{optimizer{maxIterations=-1}}\n"
      "timeBaseSensor=a\n"
    );
  auto c = createBatchCalibrator(vsCalib, std::shared_ptr<Model>(&m, sm::null_deleter()));

  const double startTime = 0, endTime = 1.0;
  for (auto& p : mmcs.getPoses(startTime, endTime)) {
    mcSensorA.addMeasurement(p.q, p.p, p.time);
    c->addMeasurementTimestamp(p.time, mcSensorA);
  }
  c->calibrate();

  EXPECT_NEAR(5.0, mcSensorA.getTranslationToParent()[1], 0.0001);
  EXPECT_TRUE(EIGEN_MATRIX_NEAR(
      -mcSensorA.getTranslationToParent(),
      traj.getCurrentTrajectory().getTranslationSpline().template getEvaluatorAt<0>(0).eval(),
      1e-8
    ));
}

TEST(TestCalibration, testEstimateTwoPoseSensors) {
  auto vs = ValueStoreRef::fromString(
      "Gravity{used=false}"
      "a{frame=body,targetFrame=world,rotation/used=false,translation/used=false,delay/used=false}"
      "b{frame=body,targetFrame=world,rotation{used=true,yaw=0.1,pitch=0.,roll=0.},translation{used=true,x=0,y=5,z=0},delay/used=false}"
      "traj{frame=body,referenceFrame=world,McSensor=a,initWithPoseMeasurements=true,splines{knotsPerSecond=5,rotSplineOrder=4,rotFittingLambda=0.001,transSplineOrder=4,transFittingLambda=0.001}}"
    );

  FrameGraphModel m(vs, nullptr, {&world, &body});
  PoseSensor mcSensorA(m, "a", vs);
  PoseSensor mcSensorB(m, "b", vs);
  PoseTrajectory traj(m, "traj", vs);
  m.addModulesAndInit(mcSensorA, mcSensorB, traj);

  EXPECT_EQ(2, m.getCalibrationVariables().size());
  EXPECT_DOUBLE_EQ(5.0, mcSensorB.getTranslationToParent()[1]);
  EXPECT_NEAR(0.1, std::abs(sm::kinematics::quat2AxisAngle(mcSensorB.getRotationQuaternionToParent())[2]), 1e-6); // abs for conventional neutrality

  MockMotionCaptureSource mmcs([](Timestamp start, Timestamp now, MotionCaptureSource::PoseStamped & p){
    p.q = sm::kinematics::axisAngle2quat({double(now - start), 0, 0});
    p.p = Eigen::Vector3d::UnitX() * (now - start);
  });

  auto vsCalib = ValueStoreRef::fromString(
      "verbose=true\n"
      "acceptConstantErrorTerms=true\n"
      "timeBaseSensor=a\n"
    );
  auto c = createBatchCalibrator(vsCalib, std::shared_ptr<Model>(&m, sm::null_deleter()));

  const double startTime = 0, endTime = 1.0;

  for (auto& p : mmcs.getPoses(startTime, endTime)) {
    mcSensorA.addMeasurement(p.q, p.p, p.time);
    c->addMeasurementTimestamp(p.time, mcSensorA);
    mcSensorB.addMeasurement(p.q, p.p, p.time);
  }
  c->calibrate();

  EXPECT_NEAR(0, mcSensorB.getTranslationToParent()[1], 0.0001);
  EXPECT_NEAR(0, sm::kinematics::quat2AxisAngle(mcSensorB.getRotationQuaternionToParent())[2], 0.0001);
}

TEST(TestCalibration, testEstimateMotionCaptureSensorInit) {
  auto vs = ValueStoreRef::fromString(
      "Gravity{used=false}"
      "o{frame=world,rotation/used=false,translation/used=false,delay/used=false}"
      "a{frame=body,rotation/used=false,translation{used=true,estimate=false,x=0,y=5,z=0},delay/used=false}"
      "traj{frame=body,referenceFrame=world,McSensor=a,initWithPoseMeasurements=true,splines{knotsPerSecond=10,rotSplineOrder=4,rotFittingLambda=0.000001,transSplineOrder=4,transFittingLambda=0.0000001}}"
    );

  FrameGraphModel m(vs, nullptr, {&world, &body});
  MotionCaptureSystem observer(m, "o", vs);
  MotionCaptureSensor mcSensorA(observer, "a", vs);
  PoseTrajectory traj(m, "traj", vs);
  m.addModulesAndInit(observer, mcSensorA, traj);

  ASSERT_EQ(1, m.getCalibrationVariables().size());
  EXPECT_DOUBLE_EQ(5.0, mcSensorA.getTranslationToParent()[1]);

  MockMotionCaptureSource mmcs([](Timestamp start, Timestamp now, MotionCaptureSource::PoseStamped & p){
    p.q = sm::kinematics::quatIdentity();
    p.p = Eigen::Vector3d::UnitX() * (now - start);
  });

  mcSensorA.setMotionCaptureSource(std::shared_ptr<MotionCaptureSource>(&mmcs, sm::null_deleter()));

  auto vsCalib = ValueStoreRef::fromString(
      "verbose=true\n"
      "acceptConstantErrorTerms=true\n"
      "timeBaseSensor=a\n"
    );
  std::unique_ptr<BatchCalibratorI> c = createBatchCalibrator(vsCalib, std::shared_ptr<Model>(&m, sm::null_deleter()));

  c->addMeasurementTimestamp(0.0, mcSensorA); // add timestamps to determine the batch interval
  c->addMeasurementTimestamp(1.0, mcSensorA);
  c->calibrate();

  EXPECT_TRUE(EIGEN_MATRIX_NEAR(
      -mcSensorA.getTranslationToParent(),
      traj.getCurrentTrajectory().getTranslationSpline().template getEvaluatorAt<0>(0).eval(),
      1e-8
    ));
  EXPECT_DOUBLE_EQ(5.0, mcSensorA.getTranslationToParent()[1]);
}

TEST(TestCalibration, testEstimateMotionCaptureSensorPose) {
  auto vs = ValueStoreRef::fromString(
      "Gravity{used=false}"
      "o{frame=world,rotation/used=false,translation/used=false,delay/used=false}"
      "a{frame=body,rotation/used=false,translation/used=false,delay/used=false}"
      "b{frame=body,rotation/used=false,translation{used=true,x=0,y=5,z=0},delay/used=false}"
      "traj{frame=body,referenceFrame=world,McSensor=a,initWithPoseMeasurements=true,splines{knotsPerSecond=5,rotSplineOrder=4,rotFittingLambda=0.001,transSplineOrder=4,transFittingLambda=0.001}}"
    );
  //TODO Support some validation!
  //TODO find C++ solution to validation

  FrameGraphModel m(vs, nullptr, {&world, &body});
  MotionCaptureSystem observer(m, "o", vs);
  MotionCaptureSensor mcSensorA(observer, "a", vs);
  MotionCaptureSensor mcSensorB(observer, "b", vs);
  PoseTrajectory traj(m, "traj", vs);
  m.addModulesAndInit(observer, mcSensorA, mcSensorB, traj);

  ASSERT_EQ(1, m.getCalibrationVariables().size());


  MockMotionCaptureSource mmcs([](Timestamp start, Timestamp now, MotionCaptureSource::PoseStamped & p){
    p.q = sm::kinematics::quatIdentity();
    p.p = Eigen::Vector3d::UnitX() * (now - start);
  });

  mcSensorA.setMotionCaptureSource(std::shared_ptr<MotionCaptureSource>(&mmcs, sm::null_deleter()));
  mcSensorB.setMotionCaptureSource(std::shared_ptr<MotionCaptureSource>(&mmcs, sm::null_deleter()));

  auto vsCalib = ValueStoreRef::fromString(
      "acceptConstantErrorTerms=true\n"
      "timeBaseSensor=a\n"
    );

  EXPECT_DOUBLE_EQ(5.0, mcSensorB.getTranslationToParent()[1]);

  auto c = createBatchCalibrator(vsCalib, std::shared_ptr<Model>(&m, sm::null_deleter()));

  c->addMeasurementTimestamp(0.0, mcSensorA); // add timestamps to determine the batch interval
  c->addMeasurementTimestamp(1.0, mcSensorA);

  c->calibrate();

  EXPECT_NEAR(0, mcSensorB.getTranslationToParent()[1], 0.0001);
}
