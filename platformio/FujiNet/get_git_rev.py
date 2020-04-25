import subprocess

revision = subprocess.check_output(["git", "rev-parse", "HEAD"]).strip()
revision = revision.decode("utf-8")[:8]
print("-DGIT_SRC_REV=\\\"%s\\\"" % revision)
