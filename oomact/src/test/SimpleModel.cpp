#include <aslam/calibration/test/SimpleModel.hpp>

#include <cmath>

#include <gtest/gtest.h>

#include <aslam/calibration/model/Model.h>
#include <sm/BoostPropertyTree.hpp>
#include <sm/value_store/ValueStore.hpp>

namespace aslam {
namespace calibration {
namespace test {

SimpleModel::SimpleModel(ValueStoreRef config, std::shared_ptr<ConfigPathResolver> configPathResolver)
  : Model(config, configPathResolver),
    config_(config),
    sensorsConfig_(config.getChild("sensors")),
    wheelOdometry_(*this, "WheelOdometry", sensorsConfig_)
{
  add(wheelOdometry_);
}

}
}
}
