![mamba header image](docs/assets/mamba_header.png)

## The Fast Cross-Platform Package Manager

<table>
<thead align="center" cellspacing="10">
  <tr>
    <th colspan="3" align="center" border="">part of mamba-org</th>
  </tr>
</thead>
<tbody>
  <tr background="#FFF">
    <td align="center">Package Manager <a href="https://github.com/mamba-org/mamba">mamba</a></td>
    <td align="center">Package Server <a href="https://github.com/mamba-org/quetz">quetz</a></td>
    <td align="center">Package Builder <a href="https://github.com/mamba-org/boa">boa</a></td>
  </tr>
</tbody>
</table>

# mamba

[![Build Status](https://github.com/mamba-org/mamba/workflows/CI/badge.svg)](https://github.com/mamba-org/mamba/actions)
[![Join the Gitter Chat](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/QuantStack/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Mamba is a reimplementation of the conda package manager in C++.

- parallel downloading of repository data and package files using multi-threading
- libsolv for much faster dependency solving, a state of the art library used in the RPM package manager of Red Hat, Fedora and OpenSUSE
- core parts of mamba are implemented in C++ for maximum efficiency

At the same time, mamba utilize the same command line parser, package installation and deinstallation code and transaction verification routines as `conda` to stay as compatible as possible.

Mamba is part of a bigger ecosystem to make scientific packaging more sustainable. You can read our [announcement blog post](https://medium.com/@QuantStack/open-software-packaging-for-science-61cecee7fc23).
The ecosystem also consists of `quetz`, an open source conda package server and `boa`, a fast conda package builder.

### Installation

It's advised to install mamba from conda-forge. If you already have conda:

```
conda install mamba -c conda-forge
```

otherwise it's best to start with [Miniconda](https://docs.conda.io/en/latest/miniconda.html).
If you want to experiment with the latest software, you can also try micromamba (more below).

### Installing conda packages with `mamba`

Now you are ready to install packages with

```bash
mamba install xtensor-r -c conda-forge
```
for example.

### Additional features

Mamba comes with features on top of stock conda.
To efficiently query repositories and query package dependencies you can use `mamba repoquery`.

Here are some examples:

`mamba repoquery search xtensor` will show you all available xtensor packages. You can also specify more constraints on this search query, for example `mamba repoquery search "xtensor>=0.18"`

`mamba repoquery depends xtensor` will show you a tree view of the dependencies of xtensor.

```
$ mamba repoquery depends xtensor

xtensor == 0.21.5
  ├─ libgcc-ng [>=7.3.0]
  │ ├─ _libgcc_mutex [0.1 conda_forge]
  │ └─ _openmp_mutex [>=4.5]
  │   ├─ _libgcc_mutex already visited
  │   └─ libgomp [>=7.3.0]
  │     └─ _libgcc_mutex already visited
  ├─ libstdcxx-ng [>=7.3.0]
  └─ xtl [>=0.6.9,<0.7]
    ├─ libgcc-ng already visited
    └─ libstdcxx-ng already visited
```

And you can ask for the inverse, which packages depend on some other package (e.g. `ipython`) using `whoneeds`.

```
$ mamba repoquery whoneeds ipython

 Name            Version Build          Channel
──────────────────────────────────────────────────
 ipykernel       5.2.1   py37h43977f1_0 installed
 ipywidgets      7.5.1   py_0           installed
 jupyter_console 6.1.0   py_1           installed

```

With the `--tree` (or `-t`) flag, you can get the same information in a tree.


## micromamba

`micromamba` is a tiny version of the `mamba` package manager.
It is a pure C++ package with a separate command line interface.
It can be used to bootstrap environments (as an alternative to miniconda), but it's currently experimental.
The benefit is that it's very tiny and does not come with a default version of Python.

`micromamba` works in the bash & zsh shell on Linux & OS X.
It's completely statically linked, which allows you to drop it in some place and just execute it.

Note: it's advised to use micromamba in containers & CI only.

Download and unzip the executable (from the official conda-forge package):

```sh
wget -qO- https://micromamba.snakepit.net/api/micromamba/linux-64/latest | tar -xvj bin/micromamba
```

We can use `./micromamba shell init ... ` to initialize a shell (`.bashrc`) and a new root environment in `~/micromamba`:

```sh
./bin/micromamba shell init -s bash -p ~/micromamba
source ~/.bashrc
```

Now you can activate the base environment and install new packages, or create other environments.

Note: currently the `-c` arguments have to come at the end of the command line.

```sh
micromamba activate
micromamba install python=3.6 jupyter -c conda-forge
# or
micromamba create -p /some/new/prefix xtensor -c conda-forge
micromamba activate /some/new/prefix
```

For more instructions (including OS X) check out https://gist.github.com/wolfv/fe1ea521979973ab1d016d95a589dcde

### Development installation

Make sure to have the following requirements in your conda environment:

`mamba install cmake compilers pybind11 libsolv libarchive libcurl nlohmann_json pip cpp-filesystem yaml-cpp cpp-terminal -c conda-forge`

If you build mamba in a different environment than base, you must also install conda in
that environment:

`mamba install conda -c conda-forge`

For a local (dev) build, run `pip install -e .`. This will build and install mamba
in the conda environment.

#### cmake based build

You will additionally need to install cmake and cli11 for micromamba:

```bash
mamba install -c conda-forge cli11 cmake
```

For the C++ tests, you need Google Tests installed (e.g. `mamba install gtest`).
To build the program using CMake, the following line needs to be used:

```bash
cmake .. \
    -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX \
    -DPYTHON_EXECUTABLE=$CONDA_PREFIX/bin/python3 \
    -DPYTHON_LIBRARIES=$CONDA_PREFIX/lib/libpython3.7m.so \
    -DENABLE_TESTS=ON
```

### Support us

For questions, you can also join us on the [QuantStack Chat](https://gitter.im/QuantStack/Lobby) or on the [conda channel](https://gitter.im/conda/conda) (note that this project is not officially affiliated with `conda` or Anaconda Inc.).

### License

We use a shared copyright model that enables all contributors to maintain the copyright on their contributions.

This software is licensed under the BSD-3-Clause license. See the [LICENSE](LICENSE) file for details.
