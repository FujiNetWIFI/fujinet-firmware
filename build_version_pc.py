from __future__ import print_function

import datetime
import re
import subprocess
import sys

import version_common as vc


class Version:
    def __init__(self):
        self._git_sha = None
        self._git_sha_short = None
        self._git_date = None
        self._head_tags = None

    @property
    def git_sha_short(self):
        """return short version of commit hash"""
        if self._git_sha_short is None:
            self._git_sha_short = vc.get_commit_sha(short=True)
        return self._git_sha_short

    @property
    def git_sha(self):
        """return commit hash"""
        if self._git_sha is None:
            self._git_sha = vc.get_commit_sha(short=False)
        return self._git_sha

    @property
    def git_date(self):
        """return HEAD commit date"""
        if self._git_date is None:
            self._git_date = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")
            if self.git_sha:
                try:
                    self._git_date = subprocess.check_output(
                        ["git", "show", "--quiet", "--date=format-local:%Y-%m-%d %H:%M:%S",
                         "--format=%cd", self.git_sha],
                        env={'TZ': 'UTC0'}, universal_newlines=True).strip()
                except (FileNotFoundError, subprocess.CalledProcessError):
                    pass
        return self._git_date

    @property
    def head_tags(self):
        """return list of tags pointing at HEAD"""
        if self._head_tags is None:
            self._head_tags = vc.get_head_tags()
        return self._head_tags

    def get_commit_version(self, ver_major=0, ver_minor=0):
        """derive a short build-version string from `git describe`,
        falling back to ver_major.ver_minor when no tag is reachable"""
        describe = vc.get_commit_version()
        version, *_rest = vc.parse_describe(describe, ver_major, ver_minor)
        if vc.get_modified_files():
            version += "*"
        return version

    def load(self, version):
        """load version attributes"""
        self.version = version
        self.suffix = ""
        if version not in self.head_tags:
            # fall back to the release version's own major.minor when
            # `git describe` can't find a reachable tag at all
            m = re.match(r"^v([0-9]+)[.]([0-9]+)", version)
            ver_major = int(m.group(1)) if m else 0
            ver_minor = int(m.group(2)) if m else 0
            descr = self.get_commit_version(ver_major, ver_minor)
            if descr:
                self.version = descr
            elif self.git_sha_short:
                self.suffix = "+git-" + self.git_sha_short
            else:
                self.suffix = "-nogit"

    @property
    def version_full(self):
        return self.version + self.suffix


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
            m = re.match(vc.MACRO_PATTERN, line)
            if m and m.group(1) == "FN_VERSION_FULL":
                version_str = m.group(2).strip().strip('"')
    return version_str


def main():

    fn_version = get_version_from_file(sys.argv[1])

    ver = Version()
    ver.load(fn_version)

    print("FujiNet release version:", fn_version)
    print("FujiNet build git version:", ver.version_full)
    print("FujiNet build git date:", ver.git_date)

    return(create_build_version_file(sys.argv[2], ver))


if __name__ == '__main__':
    sys.exit(main() or 0)
