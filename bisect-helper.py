#!/usr/bin/env python3
import argparse
import subprocess
import os, sys
import pty
import tty, termios
import signal
import select

GOOD = 0
BAD  = 1
SKIP = 125

BANNER_WIDTH = 40

BUILD = "./build.sh"
FNCONFIG = "fnconfig.ini"

BUILD_exit_on_error_patch = """diff --git a/build.sh b/build.sh
index cba114136..e6a8d41d0 100755
--- a/build.sh
+++ b/build.sh
@@ -1,2 +1,3 @@
 #!/bin/bash
+set -e
"""

class BisectParser(argparse.ArgumentParser):
  def error(self, message):
    sys.stderr.write(f"error: {message}\n")
    self.print_usage(sys.stderr)
    # Exit with 255 so the git bisect run stops
    self.exit(255)

def build_argparser():
  parser = BisectParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("patch", nargs="*", help="patches to apply")
  parser.add_argument("--anypatch", action="store_true", help="not all patches need to apply")
  parser.add_argument("--esp32", action="store_true", help="build for ESP32")
  parser.add_argument("--skip-fs", action="store_true", help="don't flash the filesystem")
  parser.add_argument("--compile-only", action="store_true", help="don't run, just compile")
  parser.add_argument("--platform", default="COCO", help="LWM platform to build")
  parser.add_argument("--fnconfig", default=FNCONFIG, help="fnconfig.ini to use")

  group = parser.add_mutually_exclusive_group()
  group.add_argument("--build-err-skip", dest="build_err", action="store_const",
                     const="skip", help="skip commit if build fails")
  group.add_argument("--build-err-bad",  dest="build_err", action="store_const",
                     const="bad",  help="mark commit bad if build fails")
  parser.set_defaults(build_err="skip")
  return parser

def wait_for_quit(cmd, cmd_dir, quit_key=None):
  parent_pty, child_pty = pty.openpty()
  proc = subprocess.Popen(
    cmd,
    cwd=cmd_dir,
    stdin=child_pty,
    stdout=child_pty,
    stderr=subprocess.STDOUT,
    close_fds=True,
    text=False,  # binary mode for raw bytes
    preexec_fn=lambda: os.setsid(), # Use setsid() to put the child in a new process group.
  )
  print(f"Process PID is: {proc.pid}")
  os.close(child_pty)

  fd = sys.stdin.fileno()
  old_settings = termios.tcgetattr(fd) # Save current terminal settings
  tty.setraw(fd)

  if quit_key:
    print(f"Press '{quit_key}' to stop the test program...")
    quit_key = ord(quit_key)

  try:
    while True:
      rlist, _, _ = select.select([sys.stdin, parent_pty], [], [])
      if parent_pty in rlist:
        data = os.read(parent_pty, 1024)
        if not data:
          break
        os.write(sys.stdout.fileno(), data)
      else:
        key = os.read(sys.stdin.fileno(), 1)
        if key[0] == 3:
          os.killpg(os.getpgid(proc.pid), signal.SIGINT)

        if key[0] == quit_key:
          break

        os.write(parent_pty, key)

  except OSError:
    pass

  finally:
    os.close(parent_pty)
    proc.wait()
    termios.tcsetattr(fd, termios.TCSADRAIN, old_settings) # Restore original settings

  return

def run_cmd(cmd, **kwargs):
  """Run a shell command, raising an error if it fails."""
  subprocess.run(cmd, check=True, **kwargs)
  return

def check_patches(patches):
  """Check if all patches can be applied cleanly."""
  try:
    for patch in patches:
      run_cmd(["git", "apply", "--check", patch])
  except subprocess.CalledProcessError:
    return False
  return True

def apply_patches(patches):
  """Apply patches in order."""
  for patch in patches:
    try:
      print(f"PATCH: Trying to apply {patch}...")
      run_cmd(["git", "apply", "--index", patch])
    except subprocess.CalledProcessError:
      print(f"PATCH: Unable to apply {patch}")
      pass
    else:
      print(f"PATCH: Applied {patch}")
  return

def restore_repo():
  """Restore all changes so bisect continues."""
  subprocess.run(["git", "reset", "--hard", "HEAD"])
  return

def bisectExit(code, restoreFlag):
  if restoreFlag:
    restore_repo()
  exit(code)

def get_unique_shortcuts(term_old: str, term_new: str) -> tuple[str, str]:
  """Calculates distinct single-character shortcuts for both terms."""
  str1 = term_old.lower()
  str2 = term_new.lower()

  # 1. Look for the first position where the characters differ
  for c1, c2 in zip(str1, str2):
      if c1 != c2:
          return c1, c2

  # 2. Fallback if one word is a prefix of another or identical (e.g. "fix" vs "fixed")
  c1 = str1[0] if str1 else 'g'
  # Shift the character by 1 position in the alphabet (mod 26)
  c2 = chr(((ord(c1) - ord('a') + 1) % 26) + ord('a'))
  return c1, c2

def prompt_bisect_status():
  try:
    res_old = subprocess.run(["git", "bisect", "terms", "--term-old"],
                             capture_output=True, text=True, check=True)
    res_new = subprocess.run(["git", "bisect", "terms", "--term-new"],
                             capture_output=True, text=True, check=True)
    term_old = res_old.stdout.strip()
    term_new = res_new.stdout.strip()
  except subprocess.CalledProcessError:
    term_old, term_new = "good", "bad"

  char_old, char_new = get_unique_shortcuts(term_old, term_new)

  result = None
  while result is None:
    try:
      prompt_text = f"{term_old.capitalize()}/{term_new.capitalize()}/Skip?" \
        f" ({char_old}/{char_new}/s) "
      response = input(prompt_text).strip().lower()
    except EOFError:
      result = SKIP
      break

    if response == char_old or response == term_old.lower():
      result = GOOD
    elif response == char_new or response == term_new.lower():
      result = BAD
    elif response == 's' or response == 'skip':
      result = SKIP

    print(f"Invalid input. Use '{char_old}' for {term_old}," \
          f" '{char_new}' for {term_new}, or 's' to skip.")

  return result

def main():
  args = build_argparser().parse_args()

  do_restore = False

  if not os.path.isfile(BUILD) or not os.access(BUILD, os.X_OK):
    print()
    print("#" * BANNER_WIDTH)
    print("NO build.sh FOUND".center(BANNER_WIDTH))
    print("#" * BANNER_WIDTH)
    bisectExit(SKIP, restoreFlag=do_restore)

  if args.patch:
    if args.anypatch or check_patches(args.patch):
      apply_patches(args.patch)
    else:
      print("Failed to apply patches")
      exit(255)
    do_restore = True

  cmd = [BUILD, ]
  if args.esp32:
    # FIXME - look at args.platform and fixup platformio.local.ini
    try:
      precmd = ["grep", "-q", "set -e", BUILD]
      subprocess.run(precmd, check=True)
    except subprocess.CalledProcessError:
      print()
      print("#" * BANNER_WIDTH)
      print("build.sh ignores errors, patching".center(BANNER_WIDTH))
      print("#" * BANNER_WIDTH)
      try:
        run_cmd(["patch", "-p1"], input=BUILD_exit_on_error_patch, text=True,
                 capture_output=True)
        do_restore=True
      except:
        bisectExit(SKIP, restoreFlag=do_restore)

    if args.compile_only:
      cmd.extend(["-b", ])
    else:
      cmd.extend(["-u", ])
    # PlatformIO likes to mangle sdkconfig.* files
    do_restore = True
  else:
    cmd.extend(["-p", args.platform])

  try:
    subprocess.run(cmd, check=True)
  except subprocess.CalledProcessError:
    print()
    print("#" * BANNER_WIDTH)
    print("FAILED TO BUILD".center(BANNER_WIDTH))
    print("#" * BANNER_WIDTH)
    bisectExit(SKIP if args.build_err == 'skip' and not args.compile_only else BAD,
               restoreFlag=do_restore)

  # Upload the filesystem on ESP32
  if args.esp32 and not args.skip_fs and not args.compile_only:
    cmd = [BUILD, "-f"]
    try:
      subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError:
      print()
      print("#" * BANNER_WIDTH)
      print("FAILED TO UPLOAD FILESYSTEM".center(BANNER_WIDTH))
      print("#" * BANNER_WIDTH)
      bisectExit(SKIP if args.build_err == 'skip' and not args.compile_only else BAD,
                 restoreFlag=do_restore)

  if not args.compile_only:
    # Start the test program
    if args.esp32:
      cmd = [BUILD, "-m"]
      cmd_dir = None
    else:
      cmd = ["./fujinet", "-c", os.path.abspath(args.fnconfig)]
      cmd_dir = "build/dist"
    wait_for_quit(cmd, cmd_dir)
    print()
    print()

    result = prompt_bisect_status()
    bisectExit(result, restoreFlag=do_restore)

  else:
    # If we are only compiling and didn't fail, then it's good
    bisectExit(GOOD, restoreFlag=do_restore)

if __name__ == "__main__":
  main()
