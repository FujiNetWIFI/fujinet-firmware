# build.sh documentation

build.sh is a script that has grown to provide a mechanism for building a platform, allowing
users to define the board they are building in a simple fashion, build that platform, and
deploy it to the FujiNet directly, and monitor the devices console output.

It was written to replace the need to use Visual Code PlatformIO plugin, as that is also just
a GUI around the pio application.

In order to run build.sh, you must have the file `platformio.local.ini` in the root of your
folder, which defines the board, and any additional values you require for it, to build.
The next section talks about generating this file.

In the following notes, I talk about a "full platformio ini file" deliberately without naming
it. That file is one that can be used by PIO without further changes to build the target
platform.

I also talk about "local" files, these contain only additional values that are merged into
the "full" file, and are not complete files, nor should they be. The idea is to keep your own
changes minimal and separate from the full file so you have nothing to change when any
global changes are applied from upstream (e.g. additional build python scripts, version
updates, etc.).

It should be noted that git ignores all files of the form `platformio.local*.ini` and
`platformio-release.ini` so they won't be checked into the repository. Thus when
creating multiple different local files (as discussed below), you can ensure they are not
checked into the repo by following the 'local' naming convention.

## Generating a new local ini file

You can either write your own, or use build.sh to generate a new local ini file for you.

To do the latter, run the following:

```sh
# use fujinet-atari-v1 for example
./build.sh -s fujinet-atari-v1
```

Board names can be found in `build-platforms/platformio-*.ini`

This will overwrite the default file `platformio.local.ini` so use wisely, as you will
lose any previous local changes you made.

If you add the `-y` parameter to build.sh, it will not ask if you want to create the file.
This is used in automated builds and when building all platforms using build-all.

## Template platformio ini file generation

By default (i.e. without any command line args), build.sh will use an ini file
named `platformio-generated.ini` to build.

This file is created every time build.sh is run, merging values from the following files in the following order:

- `platformio-ini-files/platformio.common.ini`
- `build-platforms/platformio-{build_board}.ini`
- `platformio.local.ini`

This allows users to keep just their changes in the git-ignored file `platformio.local.ini`, but receive upstream changes (like platformio version updates etc) when they build.

## Specifying different local file names

You can specify the local file that will be used (in both reading and generating as new) with the `-l FILE` parameter

```sh
./build.sh -s fujinet-atari-v1 -l platformio.local-atari.ini
```
The above will generate a new local file, but name it `platformio.local-atari.ini` instead
of the default `platformio.local.ini`

You can then use this file in building the application, instead of the default file. This is
useful if you have several devices, e.g. if you generate `platformio.local-apple.ini` and `platformio.local-atari.ini`, then you can build with them:

```sh
# generate the apple/atari ini files:
./build.sh -ys fujinet-atari-v1 -l platformio.local-atari.ini
./build.sh -ys fujiapple-rev0 -l platformio.local-apple.ini

# you can edit the above to add any additional flags, change monitor speed/port etc if required

# clean/build/upload/monitor ATARI platform
./build.sh -cbum -l platformio.local-atari.ini

# clean/build/upload/monitor APPLE platform
./build.sh -cbum -l platformio.local-apple.ini
```

which saves you from having to regenerate the file when you're switching machines.

## Format of local file

This is a standard ini file with one additional feature; you can specify `+=` to add to existing values, rather than fully overwriting them with local values.

```ini
[fujinet]
build_board = fujinet-atari-v1

[env]
upload_port = /dev/ttyOTHER
build_flags +=
        -D CORE_DEBUG_LEVEL=5
        -D FNCONFIG_DEBUG=1
        -D VERBOSE_HTTP

[env:fujinet-atari-v1]

build_flags +=
        -D FN_HISPEED_INDEX=0
```

Using the `-s BUILD_BOARD` option detailed previously will generate a new `platformio.local.ini` file with just the `[fujinet]` section and the named BUILD_BOARD value set.

This is the only value that MUST exist in the local file, else the build will exit with an error, as it will not know which template to use for loading board specific values from.

In the above example, `+=` was used to add values to the common `build_flags` values in other sections.

Values assigned with just `=`, will override any common or board specific values, as in the example of `env.upload_port`.

## generating specific full ini files

The default is setup so users don't have to specify any arguments to build, or worry about
ini names except for the single `platformio.local.ini` file.

However, in addition to the information above about generating a different local ini file, you can also
generate default full platformio ini files.

If you want to generate a different full platformio ini file, you can do this directly with the `-i INI_FILE_NAME` and optionally use it in combination with `-l LOCAL_FILE` to pull different local values in.

```sh
./build.sh -i platformio-my-full-generated-file.ini -l platformio.local-atari-release.ini
```

As the build always re-generates the INI file used for a build (specified with `-i`), the above command
will create the file `platformio-my-full-generated-file.ini` but not run a build, as no build args were specified (e.g. `-b` for build, `-p` for fujinet-PC build, or `-z` for release build).

This is a great way of just generating the ini files, and exiting without doing other work.

Note: Absolute and relative paths can be specified for names of files and do not have to be in the current working directory.

### Creating a release/zip build

Simply specify `-z` arg with anything else you need.

```sh
./build.sh -z
```

The `-z` argument is the flag to generate a release archive zip file.

This pulls in changes from `platformio-ini-files/platformio.zip-options.ini` over the top of the generated ini file.
