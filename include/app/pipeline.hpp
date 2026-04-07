#pragma once

#include "app/config.hpp"

namespace app {

class Pipeline {
public:
    explicit Pipeline(PipelineConfig config);
    void run();

private:
    PipelineConfig config_;
};

}  // namespace app
