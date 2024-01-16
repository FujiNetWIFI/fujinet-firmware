#!/bin/bash
# Usage: upstream.sh [target-branch-if-not-master-or-main]
#
# Fetch upstream changes, merge them in with ff-only to our main/master
# branch, then rebase our current work on top of that.
# Specify the "main" branch you want to rebase against as a parameter
# if it is NOT one of main/master.

UB_SPECIFIED="false"
if [ $# -eq 1 ] ; then
  USER_BRANCH="$1"
  UB_SPECIFIED="true"
fi

if [ -n "$(git status --short)" ] ; then
  echo "Your working tree is not clean. Aborting"
  echo "check these files:"
  git status --short
  echo "there should be no untracked or un-committed files to proceed!"
  exit 1
fi

git remote | grep -s 'upstream' > /dev/null
if [ $? -ne 0 ] ; then
  echo "No upstream remote found."
  echo "Please use 'git remote add upstream <URL to remote git repo>' to use this"
  exit 1
fi

# main or master branch name
if [ "$UB_SPECIFIED" == "true" ] ; then
  BR_MATCH="(\b$USER_BRANCH\b|\bmaster\b|\bmain\b)"
else
  BR_MATCH="(\bmaster\b|\bmain\b)"
fi
M_BRANCH=$(git branch -v --no-color | sed 's#^\*# #' | awk '{print $1}' | grep -E "$BR_MATCH" | head -1)

if [ -z "$M_BRANCH" ] ; then
  echo "Could not find branch to merge against. Used patter: $BR_MATCH"
  exit 1
fi

# jumps to master/main if needed, fetches, merges and pushes latest upstream/master changes to our
# repo, then rebases our branch on top of master/main

current_branch=$(git rev-parse --abbrev-ref HEAD)
if [ "$current_branch" != "${M_BRANCH}" ]; then
  echo "Changing from branch ${current_branch} to ${M_BRANCH}"
  git checkout ${M_BRANCH}
fi

echo "Fetching upstream changes"
git fetch upstream ${M_BRANCH}
echo "Merging changes (fast forward only)"
git merge upstream/${M_BRANCH} --ff-only
if [ $? -ne 0 ] ; then
  echo "ERROR performing ff-only merge. Rebasing against upstream"
  git rebase upstream/${M_BRANCH}
  if [ $? -ne 0 ] ; then
    echo "ERROR rebasing to upstream. Your changes conflict with upstream changes."
    git rebase --abort
    exit 1
  fi
fi

if [ "$current_branch" != "${M_BRANCH}" ]; then
  echo "Resetting back to previous branch ${current_branch}"
  git checkout $current_branch
  echo "Rebasing local changes with external. You may need to fix merge conflicts"
  git rebase ${M_BRANCH}
fi

