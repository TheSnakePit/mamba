import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from .helpers import get_umamba, random_string


def config(*args):
    umamba = get_umamba()
    cmd = [umamba, "config"] + [arg for arg in args if arg]
    res = subprocess.check_output(cmd)
    if "--json" in args:
        j = json.loads(res)
        return j

    return res.decode()


@pytest.fixture(scope="session")
def rc_file(tmpdir_factory):
    fn = tmpdir_factory.mktemp("umamba").join("config.yaml")
    content = "channels:\n  - channel1\n  - channel2\n"
    with open(fn, "w") as f:
        f.write(content)
    return fn


class TestConfig:
    def test_empty(self):
        assert "Configuration of micromamba" in config()

    @pytest.mark.parametrize("quiet_flag", ["-q", "--quiet"])
    def test_quiet(self, quiet_flag):
        assert config(quiet_flag) == ""


class TestConfigSources:

    root_prefix = Path(os.environ["MAMBA_ROOT_PREFIX"]).__str__()
    target_prefix = os.path.join(root_prefix, "envs", "source_env")
    home_dir = os.path.expanduser("~")
    rc_files = [
        # "/etc/conda/.condarc",
        # "/etc/conda/condarc",
        # "/etc/conda/condarc.d/",
        # "/etc/conda/.mambarc",
        # "/var/lib/conda/.condarc",
        # "/var/lib/conda/condarc",
        # "/var/lib/conda/condarc.d/",
        # "/var/lib/conda/.mambarc",
        os.path.join(root_prefix, ".condarc"),
        os.path.join(root_prefix, "condarc"),
        os.path.join(root_prefix, "condarc.d"),
        os.path.join(root_prefix, ".mambarc"),
        os.path.join(home_dir, ".conda", ".condarc"),
        os.path.join(home_dir, ".conda", "condarc"),
        os.path.join(home_dir, ".conda", "condarc.d"),
        os.path.join(home_dir, ".condarc"),
        os.path.join(home_dir, ".mambarc"),
        os.path.join(target_prefix, ".condarc"),
        os.path.join(target_prefix, "condarc"),
        os.path.join(target_prefix, "condarc.d"),
        os.path.join(target_prefix, ".mambarc"),
    ]

    @pytest.mark.parametrize("quiet_flag", ["-q", "--quiet"])
    @pytest.mark.parametrize("rc_file", ["", "dummy.yaml", ".mambarc"])
    @pytest.mark.parametrize("norc", [False, True])
    def test_sources(self, quiet_flag, rc_file, norc):
        rc_dir = os.path.expanduser(os.path.join("~", "test_mamba", random_string()))
        os.makedirs(rc_dir, exist_ok=True)

        if rc_file:
            rc_path = os.path.join(rc_dir, rc_file)
            with open(rc_path, "w") as f:
                f.write("override_channels_enabled: true")

            if norc:
                with pytest.raises(subprocess.CalledProcessError):
                    config("sources", quiet_flag, "--rc-file", rc_path, "--no-rc")
            else:
                res = config("sources", quiet_flag, "--rc-file", rc_path)
                rc_path_short = rc_path.replace(os.path.expanduser("~"), "~")
                assert (
                    res.strip().splitlines()
                    == f"Configuration files (by precedence order):\n{rc_path_short} (valid)".splitlines()
                )
        else:
            res = config("sources", quiet_flag)
            assert res.startswith("Configuration files (by precedence order):")

        shutil.rmtree(rc_dir)

    # TODO: test OS specific sources
    # TODO: test system located sources?
    @pytest.mark.parametrize("rc_file", rc_files)
    def test_rc_file(self, rc_file):
        tmpfiles = []
        for file in TestConfigSources.rc_files:
            folder, f = file.rsplit(os.path.sep, 1)

            if not Path(folder).exists():
                os.makedirs(folder, exist_ok=True)

            if file.endswith(".d") and Path(file).exists() and Path(file).is_dir():
                file = os.path.join(file, "test.yaml")

            if Path(file).exists() and Path(file).is_file():
                tmp_file = os.path.join(folder, "tmp_" + f)
                if Path(tmp_file).exists():
                    if Path(tmp_file).is_dir():
                        shutil.rmtree(tmp_file)
                    else:
                        os.remove(tmp_file)

                os.rename(file, tmp_file)
                tmpfiles.append((file, tmp_file))

        if not Path(TestConfigSources.target_prefix).exists():
            os.makedirs(TestConfigSources.target_prefix, exist_ok=True)

        if rc_file.endswith(".d"):
            os.makedirs(rc_file, exist_ok=True)
            rc_file = os.path.join(rc_file, "test.yaml")

        with open(os.path.expanduser(rc_file), "w") as f:
            f.write("override_channels_enabled: true")

        try:
            srcs = (
                config(
                    "sources",
                    "-r",
                    TestConfigSources.root_prefix,
                    "-p",
                    TestConfigSources.target_prefix,
                )
                .strip()
                .splitlines()
            )
            short_name = rc_file.replace(os.path.expanduser("~"), "~")
            expected_srcs = f"Configuration files (by precedence order):\n{short_name} (valid)".splitlines()
            assert srcs == expected_srcs
        finally:
            if rc_file.endswith(".d"):
                shutil.rmtree(rc_file)
            else:
                os.remove(rc_file)

            for (file, tmp_file) in tmpfiles:
                os.rename(tmp_file, file)
            shutil.rmtree(TestConfigSources.target_prefix)


class TestConfigList:
    @pytest.mark.parametrize("rc_flag", ["--no-rc", "--rc-file="])
    def test_list(self, rc_file, rc_flag):
        expected = {
            "--no-rc": "\n",
            "--rc-file=" + str(rc_file): "channels:\n  - channel1\n  - channel2\n",
        }
        if rc_flag == "--rc-file=":
            rc_flag += str(rc_file)

        assert config("list", rc_flag).splitlines() == expected[rc_flag].splitlines()

    @pytest.mark.parametrize("source_flag", ["--sources", "-s"])
    def test_list_with_sources(self, rc_file, source_flag):
        home_folder = os.path.expanduser("~")
        src = f"  # '{str(rc_file).replace(home_folder, '~')}'"
        assert (
            config("list", "--rc-file=" + str(rc_file), source_flag).splitlines()
            == f"channels:\n  - channel1{src}\n  - channel2{src}\n".splitlines()
        )

    @pytest.mark.parametrize("desc_flag", ["--descriptions", "-d"])
    def test_list_with_descriptions(self, rc_file, desc_flag):
        assert (
            config("list", "--rc-file=" + str(rc_file), desc_flag).splitlines()
            == f"# channels\n#   Define the list of channels\nchannels:\n"
            "  - channel1\n  - channel2\n".splitlines()
        )

    @pytest.mark.parametrize("desc_flag", ["--long-descriptions", "-l"])
    def test_list_with_long_descriptions(self, rc_file, desc_flag):
        assert (
            config("list", "--rc-file=" + str(rc_file), desc_flag).splitlines()
            == f"# channels\n#   The list of channels where the packages will be searched for.\n"
            "#   See also 'channel_priority'.\nchannels:\n  - channel1\n  - channel2\n".splitlines()
        )

    @pytest.mark.parametrize("group_flag", ["--groups", "-g"])
    def test_list_with_groups(self, rc_file, group_flag):
        group = (
            "# ######################################################\n"
            "# #               Channels Configuration               #\n"
            "# ######################################################\n\n"
        )

        assert (
            config("list", "--rc-file=" + str(rc_file), "-d", group_flag).splitlines()
            == f"{group}# channels\n#   Define the list of channels\nchannels:\n"
            "  - channel1\n  - channel2\n".splitlines()
        )

    def test_env_vars(self):
        os.environ["MAMBA_OFFLINE"] = "true"
        assert (
            config("list", "offline", "--no-rc", "-s").splitlines()
            == "offline: true  # 'MAMBA_OFFLINE'".splitlines()
        )

        os.environ["MAMBA_OFFLINE"] = "false"
        assert (
            config("list", "offline", "--no-rc", "-s").splitlines()
            == "offline: false  # 'MAMBA_OFFLINE'".splitlines()
        )
        os.environ.pop("MAMBA_OFFLINE")

    def test_no_env(self):

        os.environ["MAMBA_OFFLINE"] = "false"

        assert (
            config(
                "list", "offline", "--no-rc", "--no-env", "-s", "--offline"
            ).splitlines()
            == "offline: true  # 'CLI'".splitlines()
        )

        os.environ.pop("MAMBA_OFFLINE")

    def test_precedence(self):
        rc_dir = os.path.expanduser(os.path.join("~", "test_mamba", random_string()))
        os.makedirs(rc_dir, exist_ok=True)
        rc_file = os.path.join(rc_dir, ".mambarc")
        short_rc_file = rc_file.replace(os.path.expanduser("~"), "~")

        with open(rc_file, "w") as f:
            f.write("offline: true")

        try:
            assert (
                config("list", "offline", f"--rc-file={rc_file}", "-s").splitlines()
                == f"offline: true  # '{short_rc_file}'".splitlines()
            )

            os.environ["MAMBA_OFFLINE"] = "false"
            assert (
                config("list", "offline", "--no-rc", "-s").splitlines()
                == f"offline: false  # 'MAMBA_OFFLINE'".splitlines()
            )
            assert (
                config("list", "offline", f"--rc-file={rc_file}", "-s").splitlines()
                == f"offline: false  # 'MAMBA_OFFLINE' > '{short_rc_file}'".splitlines()
            )

            assert (
                config(
                    "list", "offline", f"--rc-file={rc_file}", "-s", "--offline"
                ).splitlines()
                == f"offline: true  # 'CLI' > 'MAMBA_OFFLINE' > '{short_rc_file}'".splitlines()
            )
            assert (
                config(
                    "list",
                    "offline",
                    f"--rc-file={rc_file}",
                    "--no-env",
                    "-s",
                    "--offline",
                ).splitlines()
                == f"offline: true  # 'CLI' > '{short_rc_file}'".splitlines()
            )
            assert (
                config(
                    "list", "offline", "--no-rc", "--no-env", "-s", "--offline",
                ).splitlines()
                == "offline: true  # 'CLI'".splitlines()
            )
        finally:
            if "MAMBA_OFFLINE" in os.environ:
                os.environ.pop("MAMBA_OFFLINE")
            shutil.rmtree(os.path.expanduser(os.path.join("~", "test_mamba")))
