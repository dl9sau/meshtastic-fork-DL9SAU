import configparser
import subprocess
import os
run_number = os.getenv('GITHUB_RUN_NUMBER', '0')
build_location = os.getenv('BUILD_LOCATION', 'local')

def readProps(prefsLoc):
    """Read the version of our project as a string"""

    config = configparser.RawConfigParser()
    config.read(prefsLoc)
    version = dict(config.items("VERSION"))
    verObj = dict(
        short="{}.{}.{}".format(version["major"], version["minor"], version["build"]),
        long="unset",
        long_fork="unset",
        deb="unset",
    )

    # Try to find current build SHA if if the workspace is clean.  This could fail if git is not installed
    try:
        # Pin abbreviation length to keep local builds and CI matching (avoid auto-shortening)
        sha = (
            subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"])
            .decode("utf-8")
            .strip()
        )
        isDirty = (
            subprocess.check_output(["git", "diff", "HEAD"]).decode("utf-8").strip()
        )
        suffix = sha
        # if isDirty:
        #     # short for 'dirty', we want to keep our verstrings source for protobuf reasons
        #     suffix = sha + "-d"
        # DL9SAU fork: embedded APP_VERSION carries the full "-DL9SAU." marker
        # (semver pre-release notation) so anyone Googling the version string
        # lands on this fork's repository. The hash is truncated to 3 chars to
        # keep the total within the 18-char Protobuf firmware_version field
        # (17 usable + NUL). Example: "2.7.27-DL9SAU.456" = 17 chars.
        # Tradeoff: 3-char SHA = 4096 buckets -> dev-build collisions possible,
        # release traceability still fine (few releases per year).
        verObj["long"] = "{}-DL9SAU.{}".format(verObj["short"], suffix[:3])
        # long_fork is the long, human-friendly filename marker (MeshCore-analog)
        # for GitHub-Release-Assets and git tag. Not embedded anywhere.
        verObj["long_fork"] = "{}-DL9SAU.g{}".format(verObj["short"], suffix)
        verObj["deb"] = "{}.{}~{}{}".format(verObj["short"], run_number, build_location, sha)
    except:
        # print("Unexpected error:", sys.exc_info()[0])
        # traceback.print_exc()
        verObj["long"] = "{}-DL9SAU".format(verObj["short"])
        verObj["long_fork"] = "{}-DL9SAU".format(verObj["short"])
        verObj["deb"] = "{}.{}~{}".format(verObj["short"], run_number, build_location)

    # print("firmware version " + verStr)
    return verObj


# print("path is" + ','.join(sys.path))