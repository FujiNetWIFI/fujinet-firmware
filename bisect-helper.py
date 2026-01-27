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

def build_argparser():
  parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("patch", nargs="*", help="patches to apply")
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
  for patch in patches:
    run_cmd(["git", "apply", "--check", patch])
  return

def apply_patches(patches):
  """Apply all patches in order."""
  for patch in patches:
    run_cmd(["git", "apply", patch])
  return

def restore_repo():
  """Restore all changes so bisect continues."""
  subprocess.run(["git", "restore", "--staged", "."])
  subprocess.run(["git", "restore", "."])
  return

def bisectExit(code, restoreFlag):
  if restoreFlag:
    restore_repo()
  exit(code)

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
    try:
      check_patches(args.patch)
      apply_patches(args.patch)
    except subprocess.CalledProcessError:
      pass
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
      print("build.sh is obsolete".center(BANNER_WIDTH))
      print("#" * BANNER_WIDTH)
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

    # Ask whether the result was good or bad
    while True:
      try:
        response = input("Good/Bad/Skip? (g/b/s) ").strip().lower()
      except EOFError:
        response = 's'

      if response.startswith('g'):
        bisectExit(GOOD, restoreFlag=do_restore)
      elif response.startswith('b'):
        bisectExit(BAD, restoreFlag=do_restore)
      elif response.startswith('s'):
        bisectExit(SKIP, restoreFlag=do_restore)
      else:
        print("Invalid input. Please answer 'good' or 'bad' or 'skip'.")

  else:
    # If we are only compiling and didn't fail, then it's good
    bisectExit(GOOD, restoreFlag=do_restore)

if __name__ == "__main__":
  main()
