#include <aslam/calibration/model/CalibrationVariable.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/make_shared.hpp>
#include <glog/logging.h>

#include <aslam/backend/MarginalizationPriorErrorTerm.hpp>
#include <aslam/backend/RotationQuaternion.hpp>
#include <aslam/backend/Scalar.hpp>
#include <aslam/backend/ScalarExpression.hpp>
#include <sm/kinematics/EulerAnglesYawPitchRoll.hpp>
#include <sm/kinematics/quaternion_algebra.hpp>

#include <aslam/calibration/error-terms/ErrorTermGroup.h>
#include <aslam/calibration/error-terms/MeasurementErrorTerm.h>
#include <aslam/calibration/tools/tools.h>

using aslam::backend::ScalarExpression;

namespace aslam {
namespace calibration {

const ErrorTermGroupReference CvPriorGroup = getErrorTermGroup("CvPrior");

namespace internal {

const std::vector<const char *> SingleComponent = {""};

const std::vector<const char *> DVComponentNames<backend::EuclideanPoint>::value = {"x", "y", "z"};
const std::vector<const char *> DVComponentNames<backend::RotationQuaternion>::value = {"roll", "pitch", "yaw"};

void getPTVector(const std::vector<const char*>& componentNames, std::vector<ValueHandle<double> >& v, ValueStore& pt) {
  v.resize(componentNames.size());
  for (size_t i = 0; i < componentNames.size(); i++) {
    v[i] = pt.getDouble(componentNames[i]);
  }
}

}

CalibrationVariable::~CalibrationVariable(){
}

const int CalibrationVariable::NameWidth = 20;

const char* getActivityPrefix(const CalibrationVariable& cv) {
  return (cv.getDesignVariable().isActive() ? "* " : "  ");
}

void printNiceInto(std::ostream & out, const CalibrationVariable & cv, std::function<void(int)> f){
  out.fill(' ');
  for(int j = 0; j < cv.getDimension(); ++j){
    out << getActivityPrefix(cv);
    char c = cv.getTangentComponentName(j)[0];
    out << std::setw(CalibrationVariable::NameWidth) << ((j == 0) ? cv.getName() : " ");
    out << " " << (c ? c : ' ');
    out << ":";
    out.width(8);
    f(j);
    out << std::endl;
  }
}

void CalibrationVariable::printFunctorInto(std::ostream& out, std::function<void(int)> f, int limit) const {
  int i = getIndex();
  if(i >= 0){
    assert(i + getDimension() <= limit); static_cast<void>(limit); // for NDEBUG builds
    printNiceInto(out, *this, [&](int j){ f(i+j);});
  }
}

void CalibrationVariable::printValuesNiceInto(std::ostream & out) const {
  auto p = getMinimalComponents();
  auto disp = getDisplacementToLastUpdateValue();
  printNiceInto(out, *this, [&](int i){
    out << p(i);
    if(fabs(disp(i)) > 1e-9){
      out << " (" << (disp(i) > 0 ? "+" : "") << disp(i) << ")";
    }
  });
}


Eigen::MatrixXd CalibrationVariable::getParams() const {
  Eigen::MatrixXd params;
  getDesignVariable().getParameters(params);
  return params;
}

namespace internal {
bool DVLoadTraitsBase::isUpdateable() const {
  for (auto& v : vhs)
    if (v.isUpdateable())
      return true;
  return false;
}

Eigen::VectorXd loadPacked(std::vector<ValueHandle<double>> &vhs){
  Eigen::VectorXd v(vhs.size());
  int i = 0;
  for(auto & vh : vhs){
    v(i++) = vh.get();
  }
  return v;
}
void storePacked(std::vector<ValueHandle<double>> &vhs, const Eigen::VectorXd & vPacked){
  int i = 0;
  for(auto & vh : vhs){
    if(vh.isUpdateable()){
      vh.update(vPacked[i]);
    }else{
      LOG(WARNING) << "Trying to update a non updatable value handler." ;
    }
    i++;
  }
}

Eigen::Vector4d ParamsPackTraits<backend::RotationQuaternion>::unpack(const Eigen::VectorXd & v){
  return sm::kinematics::axisAngle2quat(v);
}

Eigen::VectorXd ParamsPackTraits<backend::RotationQuaternion>::pack(const Eigen::VectorXd & v){
  return sm::kinematics::quat2AxisAngle(v);
}


class RotationQuaternionLoadImpl {
 public:
  virtual ~RotationQuaternionLoadImpl() = default;
  virtual Eigen::Vector4d load(ValueStoreRef & vs) = 0;
  virtual void store(const Eigen::Vector4d & v) = 0;
  virtual bool isUpdateable() = 0;
};

class RotationQuaternionLoadImplQ : public RotationQuaternionLoadImpl, public internal::DVLoadTraitsBase {
 public:
  virtual ~RotationQuaternionLoadImplQ() = default;

  static const ComponentNames QuaternionComponents;

  Eigen::Vector4d load(ValueStoreRef & vs) override {
    getPTVector(QuaternionComponents, vhs, vs);
    return switchQuaternionConvention(internal::loadPacked(vhs));
  }
  void store(const Eigen::Vector4d & v) override {
    internal::storePacked(vhs, switchQuaternionConvention(v));
  }
  bool isUpdateable() override {
    return internal::DVLoadTraitsBase::isUpdateable();
  }
  static bool switchConvention;
 private:
  static Eigen::Vector4d switchQuaternionConvention(const Eigen::Vector4d & q) {
    if(needsConjugation()){
      return sm::kinematics::quatInv(q);
    } else {
      return q;
    }
  }

  static bool needsConjugation() {
    return switchConvention;
  }
};
const ComponentNames RotationQuaternionLoadImplQ::QuaternionComponents = {"i", "j", "k", "w"};

enum class QuaternionConvention{
  HAMILTON, JPL
};

const QuaternionConvention InternalConvention = QuaternionConvention::JPL;
const QuaternionConvention DefaultExternalConvention = QuaternionConvention::HAMILTON;
bool RotationQuaternionLoadImplQ::switchConvention = DefaultExternalConvention != InternalConvention;
void setExternalQuaternionConvention(QuaternionConvention convention) {
  RotationQuaternionLoadImplQ::switchConvention = convention != InternalConvention;
}
void useJPLQuaternionConventionForInputOutput() {
  setExternalQuaternionConvention(QuaternionConvention::JPL);
}

class RotationQuaternionLoadImplRPY : public RotationQuaternionLoadImpl, public internal::DVLoadTraitsBase {
 public:
  virtual ~RotationQuaternionLoadImplRPY() = default;

  Eigen::Vector4d load(ValueStoreRef & vs) override {
    getPTVector<backend::RotationQuaternion>(vhs, vs);
    return ParamsPackTraits<backend::RotationQuaternion>::unpack(internal::loadPacked(vhs));
  }
  void store(const Eigen::Vector4d & v) override {
    internal::storePacked(vhs, ParamsPackTraits<backend::RotationQuaternion>::pack(v));
  }
  bool isUpdateable() override {
    return internal::DVLoadTraitsBase::isUpdateable();
  }
};

DVLoadTraits<backend::RotationQuaternion>::DVLoadTraits() {
}
DVLoadTraits<backend::RotationQuaternion>::~DVLoadTraits() {
}
Eigen::Vector4d DVLoadTraits<backend::RotationQuaternion>::load(ValueStoreRef & vs){
  if(!impl){
    if (vs.hasKey("yaw")) {
      impl.reset(new RotationQuaternionLoadImplRPY);
    } else {
      impl.reset(new RotationQuaternionLoadImplQ);
    }
  }
  return impl->load(vs);
}
void DVLoadTraits<backend::RotationQuaternion>::store(const Eigen::Vector4d & v) {
  CHECK(impl) << "store must not be called before load!";
  impl->store(v);
}
bool DVLoadTraits<backend::RotationQuaternion>::isUpdateable() const {
  CHECK(impl) << "isUpdateable must not be called before load!";
  return impl->isUpdateable();
}


boost::shared_ptr<backend::ErrorTerm> PriorErrorTermCreater<backend::Scalar>::createPriorErrorTerm(CalibrationDesignVariable<backend::Scalar> &dv, Eigen::MatrixXd covSqrt){
  return boost::make_shared<MeasurementErrorTerm<1, ScalarExpression>>(dv.toExpression(), dv.getParams()(0, 0), covSqrt, CvPriorGroup, true);
}

using backend::MarginalizationPriorErrorTerm;
class QuaternionPriorErrorTerm : public MarginalizationPriorErrorTerm, public ErrorTermGroupMember {
 public:
  QuaternionPriorErrorTerm(const std::vector<aslam::backend::DesignVariable*>& designVariables,
                           const Eigen::VectorXd& d,
                           const Eigen::MatrixXd& R,
                           ErrorTermGroupReference r) : MarginalizationPriorErrorTerm(designVariables, d, R), ErrorTermGroupMember(r)
  {
  }

  virtual ~QuaternionPriorErrorTerm() = default;
};

boost::shared_ptr<backend::ErrorTerm> PriorErrorTermCreater<backend::RotationQuaternion>::createPriorErrorTerm(CalibrationDesignVariable<backend::RotationQuaternion> &dv,  Eigen::MatrixXd covSqrt) {
  auto err = boost::make_shared<QuaternionPriorErrorTerm>(std::vector<backend::DesignVariable*>({&dv}), Eigen::Vector3d::Zero(), Eigen::Matrix3d::Identity(), CvPriorGroup);
  err->vsSetInvR((covSqrt * covSqrt.transpose()).inverse().eval());
  return err;
}
}

Covariance::Covariance(ValueStoreRef valueStore, int dim, bool load) {
  if(!load){
    return;
  }
  std::string s = valueStore.getString("sigma", std::string());
  if(s.empty()){
    covarianceSqrt.setIdentity(dim, dim);
  } else {
    const int commas = std::count(s.begin(), s.end(), ',');
    if(commas == 0){
      covarianceSqrt.setIdentity(dim, dim);
      covarianceSqrt *= valueStore.getDouble("sigma").get();
    } else if(commas == dim - 1 || commas == dim * dim - 1){
      boost::algorithm::replace_all(s, " ", "");
      covarianceSqrt.setIdentity(dim, dim);
      std::vector<std::string> parts = splitString(s, ",");
      const bool isDiagOnly = (parts.size() == static_cast<size_t>(dim));
      int i = 0;
      CHECK(isDiagOnly || parts.size() == static_cast<size_t>(dim*dim));
      for(auto p : parts){
        if(isDiagOnly){
          covarianceSqrt(i, i) = std::stod(p.c_str());
        } else {
          covarianceSqrt(i / dim, i % dim) = std::stod(p.c_str());
        }
        i ++;
      }
    } else {
      throw std::runtime_error("Could not parse sigma value '" + s + "'");
    }
  }
}

bool isDiagonal(const Eigen::MatrixXd & x, const double threshold) {
  Eigen::MatrixXd xc = x;
  xc.diagonal().setZero();
  return xc.norm() < threshold;
}

std::ostream & operator << (std::ostream & o, const Covariance &c){
  const auto & vsqrt = c.getValueSqrt();
  o.fill(' ');
  if (isDiagonal(vsqrt, 1e-10)) {
    o << "diag(" << vsqrt.diagonal().transpose() << ")";
  }
  else {
    o << "[";
    for (int i = 0; i < vsqrt.rows(); i++){
      if (i) o << "; ";
      o << vsqrt.row(i);
    }
    o << "]";
  }
  o << "^2";
  return o;
}

}
}
