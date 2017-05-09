/*
 * CalibrationVariable.h
 *
 *  Created on: Oct 17, 2014
 *      Author: hannes
 */

#ifndef SRC_DESIGN_VARIABLES_CALIBRATIONVARIABLE_H_
#define SRC_DESIGN_VARIABLES_CALIBRATIONVARIABLE_H_

#include <Eigen/Core>
#include <string>
#include <ostream>

#include <sm/boost/null_deleter.hpp>
#include <boost/optional.hpp>
#include <boost/make_shared.hpp>

#include <aslam/backend/DesignVariable.hpp>
#include <aslam/backend/ExpressionErrorTerm.hpp>

#include <sm/value_store/ValueStore.hpp>
#include <aslam/calibration/error-terms/ErrorTermGroup.h>
#include <aslam/calibration/error-terms/MeasurementErrorTerm.h>

namespace aslam {
namespace calibration {

using namespace ::sm::value_store;

class Covariance {
 public:
  Covariance(ValueStoreRef valueStore, int dim);

  const Eigen::MatrixXd & getValueSqrt() const{
    return covarianceSqrt;
  }
  Eigen::MatrixXd getValue() const{
    return covarianceSqrt.transpose() * covarianceSqrt;
  }
 private:
  Eigen::MatrixXd covarianceSqrt;
};

class CalibrationVariable {
 public:
  virtual ~CalibrationVariable();

  virtual const std::string & getName() const = 0;

  virtual void updateStore() = 0;
  virtual void resetToStore() = 0;
  virtual boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm() = 0;

  void printFunctorInto(std::ostream& out, std::function<void(int)> f, int limit) const;

  virtual void printValuesNiceInto(std::ostream& out) const;
  virtual backend::DesignVariable & getDesignVariable() = 0;
  virtual const backend::DesignVariable & getDesignVariable() const = 0;

  virtual const char * getTangentComponentName(int i) const = 0;
  int getDimension() const { return getDesignVariable().minimalDimensions(); }
  int getNumParams() const { return getParams().size(); }
  void setIndex(int i) { _index = i; }
  int getIndex() const { return _index; }
  Eigen::MatrixXd getParams() const;
  virtual Eigen::VectorXd getMinimalComponents() const = 0;
  virtual void setMinimalComponents(const Eigen::VectorXd & v) = 0;
  virtual Eigen::VectorXd getDisplacementToLastUpdateValue() const = 0;
  virtual double getDistanceToLastUpdateValue() const { return getDisplacementToLastUpdateValue().norm(); }
  virtual bool isUpdateable() const { return false; };
  virtual bool isToBeEstimated() const = 0;
  virtual bool isActivated() const = 0;
  static int const NameWidth;
 private:
  void printBasisInto(std::ostream& out, const Eigen::MatrixXd& mat) const;
  int _index = -1;
};


constexpr const char * SingleComponent[] = {""};

template <typename DesignVariable_>
struct DVComponentNames{
  static constexpr decltype(SingleComponent) & value = SingleComponent;
};

template <typename T, size_t N>
constexpr size_t countof(T(&)[N])
{
  return N;
}

template <typename DesignVariable_> int getDim(){
  return countof(DVComponentNames<DesignVariable_>::value);
}

template <typename DesignVariable_>
void getPTVector(std::vector<ValueHandle<double>> & v, ValueStore & pt){
  auto & componentNames = DVComponentNames<DesignVariable_>::value;
  v.resize(countof(componentNames));
  int i = 0;
  for(auto & cname : componentNames){
    v[i++] = pt.getDouble(cname);
  }
}

template <typename DesignVariable>
struct ParamsPackTraits {
  static Eigen::VectorXd pack(const Eigen::VectorXd &v) { return v; }
  static Eigen::VectorXd unpack(const Eigen::VectorXd &v) { return v; }
};

namespace internal {
  Eigen::VectorXd loadPacked(std::vector<ValueHandle<double>> &vhs);
  void storePacked(std::vector<ValueHandle<double>> &vhs, const Eigen::VectorXd & vPacked);

  inline Eigen::MatrixXd toMatrixXd(double v) {
    Eigen::MatrixXd m(1, 1);
    m << v;
    return m;
  }
  inline Eigen::MatrixXd toMatrixXd(Eigen::MatrixXd m){
    return m;
  }
}
template <typename DesignVariable_>
struct DVLoadTraits {
  decltype (ParamsPackTraits<DesignVariable_>::unpack(Eigen::VectorXd())) load(ValueStoreRef & vs){
    assert(vhs.size() == 0);
    getPTVector<DesignVariable_>(vhs, vs);
    return ParamsPackTraits<DesignVariable_>::unpack(internal::loadPacked(vhs));
  }
  void store(const Eigen::VectorXd & v){
    internal::storePacked(vhs, ParamsPackTraits<DesignVariable_>::pack(v));
  }
  bool isUpdateable() const {
    for(auto & v: vhs) if(v.isUpdateable()) return true;
    return false;
  }
 private:
  std::vector<ValueHandle<double>> vhs;
};

template <typename DesignVariable_>
class CalibrationDesignVariable : virtual protected DVLoadTraits<DesignVariable_>, virtual public DesignVariable_, virtual public CalibrationVariable {
 public:
  CalibrationDesignVariable(const std::string & name, ValueStoreRef valueStore) :
    DVLoadTraits<DesignVariable_>(), DesignVariable_(DVLoadTraits<DesignVariable_>::load(valueStore)),  name_(name), covariance_(valueStore, getDimension()),
    upstreamValue_(getParams()),
    upstreamValueStore_(valueStore)
  {
    estimateVh_ = valueStore.getBool("estimate", true);
  }

  virtual ~CalibrationDesignVariable(){}

  const std::string & getName() const override { return name_; }

  boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm() override;

  const char * getTangentComponentName(int i) const override {
    assert(i < countof(DVComponentNames<DesignVariable_>::value));
    return DVComponentNames<DesignVariable_>::value[i];
  }
  Eigen::VectorXd getMinimalComponents() const override {
    return ParamsPackTraits<DesignVariable_>::pack(getParams());
  }

  void setMinimalComponents(const Eigen::VectorXd & v) override {
    Eigen::MatrixXd val = internal::toMatrixXd(ParamsPackTraits<DesignVariable_>::unpack(v));
    DesignVariable_::setParameters(val);
  }

  void updateStore() override {
    auto v = getParams();
    DVLoadTraits<DesignVariable_>::store(v);
    upstreamValue_ = v;
  }

  virtual void resetToStore() override {
    Eigen::MatrixXd params;
    DesignVariable_(DVLoadTraits<DesignVariable_>::load(upstreamValueStore_)).aslam::backend::DesignVariable::getParameters(params);
    DesignVariable_::setParameters(params);
    upstreamValue_ = params;
    covariance_ = Covariance(upstreamValueStore_, getDimension());
  }

  Eigen::VectorXd getDisplacementToLastUpdateValue() const override {
    Eigen::VectorXd v;
    DesignVariable_::minimalDifference(upstreamValue_, v);
    return v;
  }

  bool isUpdateable() const override {
    return DVLoadTraits<DesignVariable_>::isUpdateable();
  }

  bool isToBeEstimated() const override { return estimateVh_.get(); }
  bool isActivated() const override { return DesignVariable_::isActive(); }

  virtual Eigen::MatrixXd getPriorCovarianceSqrt() const {
    return covariance_.getValueSqrt();
  }

 private:
  backend::DesignVariable & getDesignVariable() override { return *this; };
  const backend::DesignVariable & getDesignVariable() const override { return *this; };
  std::string name_;
  Covariance covariance_;
  Eigen::MatrixXd upstreamValue_;
  sm::value_store::ValueHandle<bool> estimateVh_;
  ValueStoreRef upstreamValueStore_;
};


}
namespace backend{
class Scalar;
template<typename Scalar_> class GenericScalar;
class EuclideanPoint;
class RotationQuaternion;
}



namespace calibration {
template <>
struct DVComponentNames<backend::EuclideanPoint> {
  static constexpr const char * value[] = {"x", "y", "z"};
};
template <>
struct DVComponentNames<backend::RotationQuaternion> {
  static constexpr const char * value[] = {"roll", "pitch", "yaw"};
};

template <>
struct ParamsPackTraits<backend::RotationQuaternion> {
  static Eigen::VectorXd pack(const Eigen::VectorXd &v);
  static Eigen::Vector4d unpack(const Eigen::VectorXd &v);
};
template <>
struct ParamsPackTraits<backend::Scalar> {
  static const Eigen::VectorXd & pack(const Eigen::VectorXd &v) {  return v; }
  static double unpack(const Eigen::VectorXd &v) { return v[0];}
};

template <typename Scalar>
struct ParamsPackTraits<backend::GenericScalar<Scalar>> {
  static const Eigen::VectorXd & pack(const Eigen::VectorXd &v) { return v; }
  static double unpack(const Eigen::VectorXd &v) { return v[0]; }
};

Eigen::MatrixXd getMinimalComponents(const CalibrationDesignVariable<backend::RotationQuaternion> & cv);


extern const ErrorTermGroupReference CvPriorGroup;


template<typename DesignVariable_>
struct PriorErrorTermCreater {
  static boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm(CalibrationDesignVariable<DesignVariable_> &dv, Eigen::MatrixXd covSqrt) {
    return boost::make_shared<MeasurementErrorTerm<decltype(dv.toExpression())::Dimension, decltype(dv.toExpression())>>(dv.toExpression(), dv.getMinimalComponents(), covSqrt, CvPriorGroup, true);
  }
};

template<>
struct PriorErrorTermCreater<backend::RotationQuaternion> {
  static boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm(CalibrationDesignVariable<backend::RotationQuaternion> &dv, Eigen::MatrixXd covSqrt);
};

template<>
struct PriorErrorTermCreater<backend::Scalar> {
  static boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm(CalibrationDesignVariable<backend::Scalar> &dv, Eigen::MatrixXd covSqrt);
};

template<typename Scalar_>
struct PriorErrorTermCreater<backend::GenericScalar<Scalar_>> {
  static boost::shared_ptr<backend::ErrorTerm> createPriorErrorTerm(CalibrationDesignVariable<backend::GenericScalar<Scalar_>> &dv, Eigen::MatrixXd covSqrt){
    return boost::make_shared<MeasurementErrorTerm<1, aslam::backend::GenericScalarExpression<Scalar_>>>(dv.toExpression(), dv.getParams()(0, 0), covSqrt, CvPriorGroup, true);
  }
};

template<typename DesignVariable_>
boost::shared_ptr<backend::ErrorTerm> CalibrationDesignVariable<DesignVariable_>::createPriorErrorTerm() {
  return calibration::PriorErrorTermCreater<DesignVariable_>::createPriorErrorTerm(*this, getPriorCovarianceSqrt());
}

}
}

#endif /* SRC_DESIGN_VARIABLES_CALIBRATIONVARIABLE_H_ */
