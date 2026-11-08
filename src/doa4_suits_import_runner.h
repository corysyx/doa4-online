// The swimsuit import flow's Runner over the real import job (suitconv::ImportJob). The flow (doa4_suits_import_flow.h)
// sees four calls; this forwards them. The wrapper adds no callback of its own: the request's log function is the flow's and
// holds only its own shared block, so nothing here can die before the job's threads end (the job's "For the caller" rule).

#pragma once

#include <memory>

#include "doa4_suits_import_flow.h"

namespace doa4::suits_import {

// Starts the job at once and returns the Runner that owns it. Never null: a job that could not start is already done
// with kFailed. Destroying the Runner cancels the job and joins it (from the job's own callbacks, detaches).
std::unique_ptr<Runner> StartJobRunner(suitconv::ImportRequest request);

}  // namespace doa4::suits_import
