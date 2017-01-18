#include "aslam/calibration/tools/tools.h"

#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>

#include <glog/logging.h>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/make_shared.hpp>
#include <sm/kinematics/EulerAnglesYawPitchRoll.hpp>
#include <sm/kinematics/Transformation.hpp>

#include "aslam/backend/MEstimatorPolicies.hpp"
#include <Eigen/Core>

namespace aslam {
namespace calibration {

void writeToFile(const std::string & fileName, std::function<void(std::ostream &o)> writer){
  std::ofstream file(fileName);
  writer(file);
  file.close();
}

void writeStringToFile(const std::string & fileName, const std::string & content){
  writeToFile(fileName, [&](std::ostream & o){ o << content; });
}

void createDirs(const std::string & path){
  try{
    boost::filesystem::create_directories(boost::filesystem::path(path).parent_path());
  }catch(boost::filesystem::filesystem_error & e){
    LOG(ERROR) << "error when creating all directories for path='" << path << "' :" << e.what();
    throw e;
  }
}

void openStream(std::ofstream & outputFile, std::string path) {
  path += ".dat";
  createDirs(path);
  outputFile.open(path, std::ofstream::out | std::ofstream::trunc);
  if(!outputFile.is_open())
    throw std::runtime_error(std::string("could not open ") + path);
  VLOG(1) << "Writing data to " << path << ".";
  outputFile << std::fixed << std::setprecision(18);
}



backend::MEstimator::Ptr getMestimator(const std::string & name, const sm::value_store::ValueStoreRef config) {
  std::string mEstimatorName = config.getString("name");
  if (mEstimatorName.empty() || mEstimatorName == "None"){
    LOG(INFO) << "Using no M-estimator for " << name << ".";
    return boost::make_shared<backend::NoMEstimator>();
  }
  else if (mEstimatorName == "cauchy"){
    double cauchySigma2 = config.getDouble("cauchySigma2", 10);
    LOG(INFO) << "Using Cauchy M-estimator(sigma^2 = " << cauchySigma2 << ") for " << name << ".";
    return boost::make_shared<backend::CauchyMEstimator>(cauchySigma2);
  }
  else
    throw std::runtime_error("unknown M-estimator " + mEstimatorName);
}

extern std::function<Eigen::Vector3d(const Eigen::Matrix3d & m)> matrixToEuler;
std::string poseToString(const Eigen::Vector3d & trans, const Eigen::Vector4d & rot){
  std::stringstream ss;
  ss << "P(t= " << trans.transpose() << " , r=" << matrixToEuler(sm::kinematics::quat2r(rot)).transpose() << ")";
  return ss.str();
}

std::string poseToString(const sm::kinematics::Transformation & trafo){
  return poseToString(trafo.t(), trafo.q());
}

}
}
