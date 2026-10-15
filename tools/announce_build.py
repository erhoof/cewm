#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from typing import List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from changelog_card import artifact_card
from ci_release import (Artifact, CiError, load_credentials, post_discord,
                        run_artifact)

QA_CHANNEL_ID = "1537263681228771349"
QA_ROLE_ID = "1537262210118852708"


def message(artifact: Artifact, release_candidate: bool) -> str:
    title = f"**v{artifact.series} build {artifact.run_number}**"
    if release_candidate:
        headline = f"{title} [release candidate] available <@&{QA_ROLE_ID}>"
    else:
        headline = f"{title} [unstable] available"
    return (f"{headline}\n"
            f"[Download]({artifact.download_url}) (needs a GitHub account) · "
            f"[CI build]({artifact.run_url}) · "
            f"[`{artifact.sha[:7]}`]({artifact.commit_url})")


def main(argv: List[str]) -> int:
    run_id = next((a.partition("=")[2] for a in argv
                   if a.startswith("--run-id=")), "")
    if not run_id.isdigit():
        raise CiError("--run-id=<id> is required")
    release_candidate = "--rc" in argv

    token, secret = load_credentials()
    artifact = run_artifact(token, int(run_id))
    print(f"\nArtifact        : {artifact.name}")
    print(f"  version / tag : {artifact.version} -> {artifact.tag}")
    print(f"  branch / sha  : {artifact.branch} / {artifact.sha[:7]}")
    print(f"  download      : {artifact.download_url}")

    kind = "candidate" if release_candidate else "unstable"
    card = artifact_card(token, artifact, kind)
    content = message(artifact, release_candidate)
    print(f"\n{content}\n  card: {len(card) // 1024} KB\n")
    post_discord(secret, QA_CHANNEL_ID, content,
                 ping_role=QA_ROLE_ID if release_candidate else None,
                 image=card)
    print(f"Announced {artifact.name} in channel {QA_CHANNEL_ID}.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CiError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
