import json
import os
import subprocess
import urllib.request

from utils import Environment


def setup_function(function):
    os.makedirs("test/channel_zchunk", exist_ok=True)
    function.server_proc = subprocess.Popen(
        "python -m RangeHTTPServer".split(), cwd="test/channel_zchunk"
    )


def teardown_function(function):
    function.server_proc.kill()


def test_zchunk():
    for arch in ("linux-64", "osx-64", "noarch"):
        if not os.path.exists(f"test/channel_zchunk/{arch}/repodata.json.zck"):
            os.makedirs(f"test/channel_zchunk/{arch}", exist_ok=True)
            urllib.request.urlretrieve(
                f"https://conda.anaconda.org/conda-forge/{arch}/repodata.json",
                f"test/channel_zchunk/{arch}/repodata.json",
            )
            subprocess.check_call(
                [
                    "zck",
                    f"test/channel_zchunk/{arch}/repodata.json",
                    "-o",
                    f"test/channel_zchunk/{arch}/repodata.json.zck",
                ]
            )
            os.remove(f"test/channel_zchunk/{arch}/repodata.json")

    with Environment() as env:
        # first download should download all chunks
        out = env.execute(
            f"$MAMBA install --zchunk --override-channels --strict-channel-priority --dry-run -v -y xtensor -c http://127.0.0.1:8000"
        )
        lines = [l for l in out if "Missing chunks" in l]
        missing_chunks = [int(l[l.rfind(":") + 1 :]) for l in lines]
        # since we download the current repodata,
        # we don't know the number of chunks in advance.
        # let's check that at least some chunks are downloaded
        assert 0 not in missing_chunks

        # second download should not download any chunk
        out = env.execute(
            f"$MAMBA install --zchunk --override-channels --strict-channel-priority --dry-run -v -y xtensor -c http://127.0.0.1:8000"
        )
        lines = [l for l in out if "Missing chunks" in l]
        missing_chunks = [int(l[l.rfind(":") + 1 :]) for l in lines]
        assert all(v == 0 for v in missing_chunks)

        # remove last package from repodata
        # this should result in the download of one chunk or two
        lines = [l for l in out if "Opening: " in l and ".zck" in l]
        json_files = [l[l.find("Opening: ") + 9 : l.rfind(".zck")] for l in lines]
        for path in json_files:
            with open(path) as f:
                repodata = json.load(f)
            last_package = list(repodata["packages"].keys())[-1]
            del repodata["packages"][last_package]
            with open(path, "w") as f:
                json.dump(repodata, f, indent=2)
            subprocess.check_call(f"zck {path} -o {path}.zck".split())
        out = env.execute(
            f"$MAMBA install --zchunk --override-channels --strict-channel-priority --dry-run -v -y xtensor -c http://127.0.0.1:8000"
        )
        lines = [l for l in out if "Missing chunks" in l]
        missing_chunks = [int(l[l.rfind(":") + 1 :]) for l in lines]
        assert all(v <= 2 for v in missing_chunks)
