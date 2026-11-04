import copy
import unittest
from scripts.retry_macos_allocation import Retry


class AllocationRetryTests(unittest.TestCase):
    def setUp(self):
        self.Event = {"workflow_run": {"id": 10, "name": "macOS 64-bit", "status": "completed",
            "head_sha": "current", "conclusion": "failure", "head_branch": "master", "event": "push", "run_attempt": 1,
            "head_repository": {"full_name": "owner/repo"}}}
        self.Jobs = [{"id": 20, "runner_id": 0, "status": "completed", "conclusion": "cancelled", "steps": []}]
        self.Annotations = [{"message": "The job was not acquired by Runner of type hosted even after multiple attempts"}]
        self.Posts = []

    def Request(self, path, method="GET"):
        if method == "POST":
            self.Posts.append(path)
            return {}
        if path == "commits/master":return {"sha":"current"}
        return {"jobs": self.Jobs} if "/jobs?" in path else self.Annotations

    def test_confirmed_failure_retries_only_unsuccessful_jobs(self):
        self.Jobs.append({"id": 21, "runner_id": 5, "conclusion": "success", "steps": [{}]})
        Retry(self.Event, "owner/repo", self.Request)
        self.assertEqual(self.Posts, ["actions/runs/10/rerun-failed-jobs"])

    def test_real_failure_and_manual_cancellation_are_not_retried(self):
        for annotation in ([], [{"message": "Cancelled by a user"}]):
            self.Annotations = annotation
            Retry(self.Event, "owner/repo", self.Request)
        self.Annotations = [{"message": "The job was not acquired by Runner of type hosted"}]
        self.Jobs[0]["steps"] = [{"name": "Compile", "conclusion": "failure"}]
        Retry(self.Event, "owner/repo", self.Request)
        self.assertFalse(self.Posts)

    def test_untrusted_refs_and_retry_limit_are_rejected(self):
        for key, value in (("head_branch", "feature"), ("event", "pull_request"),
                           ("run_attempt", 3), ("status", "in_progress")):
            event = copy.deepcopy(self.Event)
            event["workflow_run"][key] = value
            Retry(event, "owner/repo", self.Request)
        event = copy.deepcopy(self.Event)
        event["workflow_run"]["head_repository"]["full_name"] = "fork/repo"
        Retry(event, "owner/repo", self.Request)
        self.assertFalse(self.Posts)

    def test_mixed_source_failure_does_not_rerun_the_source_job(self):
        self.Jobs.append({"id": 21, "runner_id": 5, "status": "completed",
                          "conclusion": "failure", "steps": [{"name": "Tests"}]})
        Retry(self.Event, "owner/repo", self.Request)
        self.assertFalse(self.Posts)

    def test_superseded_commit_is_not_requeued(self):
        self.Event['workflow_run']['head_sha']='old'
        Retry(self.Event,'owner/repo',self.Request)
        self.assertFalse(self.Posts)
