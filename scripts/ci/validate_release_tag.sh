#!/usr/bin/env bash

set -euo pipefail

# A GitHub release event carries exactly one tag, so this validator is a true
# singleton rather than a batch transform.
if [[ $# -ne 1 ]]; then
    echo "usage: $0 YYYY.MM.DD[.N]" >&2
    exit 1
fi

release_tag="$1"
release_pattern='^(20[0-9]{2})\.(0[1-9]|1[0-2])\.(0[1-9]|[12][0-9]|3[01])(\.([2-9]|[1-9][0-9]+))?$'

if [[ ! "$release_tag" =~ $release_pattern ]]; then
    echo "Invalid rollNW release tag '$release_tag': expected YYYY.MM.DD or YYYY.MM.DD.N with N >= 2" >&2
    exit 1
fi

calendar_date="${BASH_REMATCH[1]}-${BASH_REMATCH[2]}-${BASH_REMATCH[3]}"
normalized_date="$(date -u -d "$calendar_date" +%Y-%m-%d 2>/dev/null || true)"
if [[ "$normalized_date" != "$calendar_date" ]]; then
    echo "Invalid rollNW release tag '$release_tag': '$calendar_date' is not a calendar date" >&2
    exit 1
fi

echo "$release_tag"
