#ifndef H481BD7A1_2233_4044_B4F2_CDD02D575958
#define H481BD7A1_2233_4044_B4F2_CDD02D575958

#include <aslam/calibration/algo/MotionCaptureSource.hpp>
#include <functional>
#include <vector>

namespace aslam {
namespace calibration {
namespace test {

class MockMotionCaptureSource : public MotionCaptureSource {
 public:
  MockMotionCaptureSource(std::function<void(Timestamp start, Timestamp now, PoseStamped & p)> func) : func(func){}
  virtual ~MockMotionCaptureSource();
  virtual std::vector<PoseStamped> getPoses(Timestamp from, Timestamp till) const override;
 private:
  std::function<void(Timestamp start, Timestamp now, PoseStamped & p)> func;
};

extern MockMotionCaptureSource mmcsStraightLine;

extern MockMotionCaptureSource mmcsRotatingStraightLine;

} /* namespace test */
} /* namespace calibration */
} /* namespace aslam */

#endif /* H481BD7A1_2233_4044_B4F2_CDD02D575958 */
