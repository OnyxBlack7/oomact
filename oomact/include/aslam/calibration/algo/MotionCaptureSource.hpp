#ifndef H7EB2DC92_140D_402E_B6E8_261C67313C3A
#define H7EB2DC92_140D_402E_B6E8_261C67313C3A

#include <vector>
#include <Eigen/Dense>

#include "../CommonTypes.hpp"

namespace aslam {
namespace calibration {

class MotionCaptureSource {
 public:
  struct PoseStamped {
    Timestamp time;
    Eigen::Vector3d p;
    Eigen::Vector4d q;
  };

  virtual std::vector<PoseStamped> getPoses(Timestamp from, Timestamp till) const = 0;
  virtual ~MotionCaptureSource(){}
};

}
}

#endif /* H7EB2DC92_140D_402E_B6E8_261C67313C3A */
