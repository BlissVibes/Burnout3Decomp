"""Where the tests that run the fork's recompilers find them.

A test names the tool with a variable (B3_EERECOMP, B3_VURECOMP). Without the variable it takes the
binary tools/port.sh translates the game with, ~/b3/ps2-web-recomp/build/tools/<name>, so a plain test
run checks the recompilers the pipeline really uses. Where neither exists (a box without the fork,
Windows python) the test is skipped.
"""
import os

BUILT = ('b3', 'ps2-web-recomp', 'build', 'tools')


def find(variable, name, environ=os.environ, home=None):
    """The tool's path, or None when the variable is unset and the fork has not built it."""
    path = environ.get(variable)
    if path:
        return path
    built = os.path.join(home or os.path.expanduser('~'), *BUILT, name)
    return built if os.path.isfile(built) else None


def missing(variable, name):
    """The reason a test is skipped for."""
    return '%s is not set and ~/%s/%s is not built' % (variable, '/'.join(BUILT), name)
