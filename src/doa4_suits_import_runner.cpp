// The swimsuit import flow's Runner over suitconv::ImportJob. See doa4_suits_import_runner.h.

#include "doa4_suits_import_runner.h"

#include <utility>

#include "doa4_suitconv_job.h"

namespace doa4::suits_import {

namespace {

class JobRunner final : public Runner {
 public:
  explicit JobRunner(std::unique_ptr<suitconv::ImportJob> job) : job_(std::move(job)) {}

  suitconv::Progress progress() const override { return job_->progress(); }
  bool done() const override { return job_->done(); }
  void Cancel() override { job_->Cancel(); }
  suitconv::ImportResult result() const override { return job_->result(); }

 private:
  std::unique_ptr<suitconv::ImportJob> job_;
};

}  // namespace

std::unique_ptr<Runner> StartJobRunner(suitconv::ImportRequest request) {
  return std::make_unique<JobRunner>(suitconv::ImportJob::Start(std::move(request)));
}

}  // namespace doa4::suits_import
