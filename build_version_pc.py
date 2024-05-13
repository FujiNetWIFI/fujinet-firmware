from __future__ import print_function

import datetime
import re
import subprocess
import sys

class Version:
    def __init__(self):

        # @property git_sha
        # @property git_sha_short
        # @property git_date
        # @property head_tags
        self._git_sha = None
        self._git_sha_short = None
        self._git_date = None
        self._head_tags = None

    def load(self, version):
        """load version attributes"""
        self.version = version
        self.suffix = ""
        if version not in self.head_tags:
            if self.git_sha_short:
                self.suffix = "+git-" + self.git_sha_short
            else:
                self.suffix = "-nogit"

    @property
    def version_full(self):
        return self.version + self.suffix

    @property
    def git_sha_short(self):
        """return short version of commit hash"""
        if self._git_sha_short is None:
            try:
                self._git_sha_short = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], universal_newlines=True).strip()
            except (FileNotFoundError, subprocess.CalledProcessError):
                self._git_sha_short = ""
            # print("SHA short:", self._git_sha_short)
        return self._git_sha_short

    @property
    def git_sha(self):
        """return commit hash"""
        if self._git_sha is None:
            try:
                self._git_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], universal_newlines=True).strip()
            except (FileNotFoundError, subprocess.CalledProcessError):
                self._git_sha = ""
            # print("SHA:", self._git_sha)
        return self._git_sha

    @property
    def git_date(self):
        """return HEAD commit date"""
        if self._git_date is None:
            #self._git_date = ""
            self._git_date = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")
            if self.git_sha:
                try:
                    self._git_date = subprocess.check_output(
                        ["git", "show", "--quiet", "--date=format-local:%Y-%m-%d %H:%M:%S", "--format=%cd", self.git_sha],
                        env={'TZ': 'UTC0'}, universal_newlines=True).strip()
                    # print("HEAD date:", self._git_date)
                except (FileNotFoundError, subprocess.CalledProcessError):
                    pass
        return self._git_date

    @property
    def head_tags(self):
        """return list of tags pointing to HEAD"""
        if self._head_tags is None:
            try:
                self._head_tags = subprocess.check_output(["git", "tag", "--points-at", "HEAD"], universal_newlines=True).splitlines()
            except (FileNotFoundError, subprocess.CalledProcessError):
                self._head_tags = []
            # print("HEAD tags:", self._head_tags)
        return self._head_tags


def create_build_version_file(filename, ver):
    defs = [
        ('FN_VERSION_FULL_GIT', ver.version_full),
        ('FN_BUILD_GIT_DATE', ver.git_date or "nogit"),
        ('FN_BUILD_GIT_SHA', ver.git_sha or "nogit"),
        ('FN_BUILD_GIT_SHA_SHORT', ver.git_sha_short or "nogit"),
    ]

    print("Writing", filename)
    with open(filename, "wt") as fout:
        for df, val in defs:
            line = '#define {} "{}"\n'.format(df, val)
            fout.write(line)
    return 0

def get_version_from_file(filename):
    version_str = ""
    print("Reading", filename)
    with open(filename, "rt") as fin:
        for line in fin:
            if line.startswith("#define FN_VERSION_FULL "):
                version_str = line[24:].strip().strip('"')
    return version_str


def main():

    fn_version = get_version_from_file(sys.argv[1])

    ver = Version()
    ver.load(fn_version)

    # print("date:", ver.date)
    print("FujiNet release version:", fn_version)
    print("FujiNet build git version:", ver.version_full)
    print("FujiNet build git date:", ver.git_date)

    return(create_build_version_file(sys.argv[2], ver))


if __name__ == '__main__':
    sys.exit(main() or 0)
