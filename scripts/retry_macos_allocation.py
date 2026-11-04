"""Retry confirmed, pre-step hosted-runner allocation failures; never source failures."""
import json
import os
import re
import urllib.request

WORKFLOWS = {"macOS 64-bit", "macOS Release Suite"}
MAX_ATTEMPTS = 3


def Retry(event, repository, request):
    run = event.get("workflow_run", {})
    if (run.get("name") not in WORKFLOWS
            or run.get("status") != "completed"
            or run.get("conclusion") not in {"failure", "cancelled"}
            or run.get("head_branch") != "master"
            or run.get("event") not in {"push", "workflow_dispatch"}
            or run.get("head_repository", {}).get("full_name") != repository
            or not 1 <= run.get("run_attempt", 0) < MAX_ATTEMPTS):
        return "No eligible completed macOS run"
    run_id = run.get("id")
    if not isinstance(run_id, int) or run_id <= 0:
        raise ValueError("Invalid workflow run ID")
    if request("commits/master").get("sha") != run.get("head_sha"):
        return "Run was superseded by a newer master commit"
    jobs = []
    page = 1
    while True:
        rows = request(f"actions/runs/{run_id}/jobs?filter=latest&per_page=100&page={page}")["jobs"]
        jobs.extend(rows)
        if len(rows) < 100:
            break
        page += 1
        if page > 20:
            raise ValueError("Unexpected workflow job count")
    failed = [job for job in jobs if job.get("conclusion") in {"failure", "cancelled", "timed_out", "action_required"}]
    if not failed:
        return "No unsuccessful jobs"
    for job in failed:
        if job.get("steps") or job.get("runner_id") != 0 or job.get("status") != "completed":
            return "A job executed steps or has a non-allocation failure; no automatic retry"
        job_id = job.get("id")
        if not isinstance(job_id, int) or job_id <= 0:
            raise ValueError("Invalid job ID")
        annotations = request(f"check-runs/{job_id}/annotations?per_page=100")
        if not any("job was not acquired by runner of type hosted" in a.get("message", "").lower() for a in annotations):
            return "Cancellation was not confirmed as a runner allocation failure"
    request(f"actions/runs/{run_id}/rerun-failed-jobs", method="POST")
    return f"Retried {len(failed)} allocation failure(s); successful jobs remain unchanged"


def Main():
    repository = os.environ["GITHUB_REPOSITORY"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository")
    with open(os.environ["GITHUB_EVENT_PATH"], encoding="utf-8") as source:
        event = json.load(source)

    def request(path, method="GET"):
        headers = {"Accept": "application/vnd.github+json", "User-Agent": "Bazzalt-CI",
                   "Authorization": "Bearer " + os.environ["GITHUB_TOKEN"]}
        req = urllib.request.Request(f"https://api.github.com/repos/{repository}/{path}",
                                     headers=headers, method=method,
                                     data=b"" if method == "POST" else None)
        with urllib.request.urlopen(req, timeout=30) as response:
            data = response.read()
        return json.loads(data) if data else {}

    print(Retry(event, repository, request))


if __name__ == "__main__":
    Main()
