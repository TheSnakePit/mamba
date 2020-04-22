#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "util.hpp"
#include "solver.hpp"
#include "pool.hpp"
#include "transaction.hpp"
#include "repo.hpp"
#include "query.hpp"
#include "subdirdata.hpp"
#include "context.hpp"

namespace py = pybind11;

PYBIND11_MODULE(mamba_api, m) {
    using namespace mamba;

    py::register_exception<mamba_error>(m, "MambaNativeException");

    py::class_<MPool>(m, "Pool")
        .def(py::init<>())
        .def("set_debuglevel", &MPool::set_debuglevel)
        .def("create_whatprovides", &MPool::create_whatprovides)
    ;

    py::class_<MRepo>(m, "Repo")
        .def(py::init<MPool&, const std::string&, const std::string&, const std::string&>())
        .def("set_installed", &MRepo::set_installed)
        .def("set_priority", &MRepo::set_priority)
        .def("name", &MRepo::name)
        .def("priority", &MRepo::priority)
        .def("size", &MRepo::size)
    ;

    py::class_<MTransaction>(m, "Transaction")
        .def(py::init<MSolver&>())
        .def("to_conda", &MTransaction::to_conda)
        .def("print", &MTransaction::print)
        .def("fetch_extract_packages", &MTransaction::fetch_extract_packages)
        .def("prompt", &MTransaction::prompt)
    ;

    py::class_<MSolver>(m, "Solver")
        .def(py::init<MPool&, std::vector<std::pair<int, int>>>())
        .def("add_jobs", &MSolver::add_jobs)
        .def("set_flags", &MSolver::set_flags)
        .def("is_solved", &MSolver::is_solved)
        .def("problems_to_str", &MSolver::problems_to_str)
        .def("solve", &MSolver::solve)
    ;

    py::class_<Query>(m, "Query")
        .def(py::init<MPool&>())
        .def("find", &Query::find)
        .def("whatrequires", &Query::whatrequires)
    ;

    py::class_<MSubdirData>(m, "SubdirData")
        .def(py::init<const std::string&, const std::string&, const std::string&>())
        .def("load", &MSubdirData::load)
        .def("loaded", &MSubdirData::loaded)
        .def("cache_path", &MSubdirData::cache_path)
    ;

    py::class_<MultiDownloadTarget>(m, "DownloadTargetList")
        .def(py::init<>())
        .def("add", [](MultiDownloadTarget& self, MSubdirData& sub) -> void {
            self.add(sub.target());
        })
        .def("download", &MultiDownloadTarget::download)
    ;

    py::class_<Context, std::unique_ptr<Context, py::nodelete>>(m, "Context")
        .def(py::init([]() {
            return std::unique_ptr<Context, py::nodelete>(&Context::instance());
        }))
        .def_readwrite("verbosity", &Context::verbosity)
        .def_readwrite("quiet", &Context::quiet)
        .def_readwrite("json", &Context::json)
        .def_readwrite("offline", &Context::offline)
        .def_readwrite("local_repodata_ttl", &Context::local_repodata_ttl)
        .def_readwrite("use_index_cache", &Context::use_index_cache)
        .def_readwrite("max_parallel_downloads", &Context::max_parallel_downloads)
        .def_readwrite("always_yes", &Context::always_yes)
        .def_readwrite("dry_run", &Context::dry_run)
        .def_readwrite("ssl_verify", &Context::ssl_verify)
        .def_readonly("sig_interrupt", &Context::sig_interrupt)
        .def_readwrite("proxies", &Context::proxies)
        .def("set_verbosity", &Context::set_verbosity)
    ;

    m.attr("SOLVER_SOLVABLE") = SOLVER_SOLVABLE;
    m.attr("SOLVER_SOLVABLE_NAME") = SOLVER_SOLVABLE_NAME;
    m.attr("SOLVER_SOLVABLE_PROVIDES") = SOLVER_SOLVABLE_PROVIDES;
    m.attr("SOLVER_SOLVABLE_ONE_OF") = SOLVER_SOLVABLE_ONE_OF;
    m.attr("SOLVER_SOLVABLE_REPO") = SOLVER_SOLVABLE_REPO;
    m.attr("SOLVER_SOLVABLE_ALL") = SOLVER_SOLVABLE_ALL;
    m.attr("SOLVER_SELECTMASK") = SOLVER_SELECTMASK;
    m.attr("SOLVER_NOOP") = SOLVER_NOOP;
    m.attr("SOLVER_INSTALL") = SOLVER_INSTALL;
    m.attr("SOLVER_ERASE") = SOLVER_ERASE;
    m.attr("SOLVER_UPDATE") = SOLVER_UPDATE;
    m.attr("SOLVER_WEAKENDEPS") = SOLVER_WEAKENDEPS;
    m.attr("SOLVER_MULTIVERSION") = SOLVER_MULTIVERSION;
    m.attr("SOLVER_LOCK") = SOLVER_LOCK;
    m.attr("SOLVER_DISTUPGRADE") = SOLVER_DISTUPGRADE;
    m.attr("SOLVER_VERIFY") = SOLVER_VERIFY;
    m.attr("SOLVER_DROP_ORPHANED") = SOLVER_DROP_ORPHANED;
    m.attr("SOLVER_USERINSTALLED") = SOLVER_USERINSTALLED;
    m.attr("SOLVER_ALLOWUNINSTALL") = SOLVER_ALLOWUNINSTALL;
    m.attr("SOLVER_FAVOR") = SOLVER_FAVOR;
    m.attr("SOLVER_DISFAVOR") = SOLVER_DISFAVOR;
    m.attr("SOLVER_JOBMASK") = SOLVER_JOBMASK;
    m.attr("SOLVER_WEAK") = SOLVER_WEAK;
    m.attr("SOLVER_ESSENTIAL") = SOLVER_ESSENTIAL;
    m.attr("SOLVER_CLEANDEPS") = SOLVER_CLEANDEPS;
    m.attr("SOLVER_ORUPDATE") = SOLVER_ORUPDATE;
    m.attr("SOLVER_FORCEBEST") = SOLVER_FORCEBEST;
    m.attr("SOLVER_TARGETED") = SOLVER_TARGETED;
    m.attr("SOLVER_NOTBYUSER") = SOLVER_NOTBYUSER;
    m.attr("SOLVER_SETEV") = SOLVER_SETEV;
    m.attr("SOLVER_SETEVR") = SOLVER_SETEVR;
    m.attr("SOLVER_SETARCH") = SOLVER_SETARCH;
    m.attr("SOLVER_SETVENDOR") = SOLVER_SETVENDOR;
    m.attr("SOLVER_SETREPO") = SOLVER_SETREPO;
    m.attr("SOLVER_NOAUTOSET") = SOLVER_NOAUTOSET;
    m.attr("SOLVER_SETNAME") = SOLVER_SETNAME;
    m.attr("SOLVER_SETMASK") = SOLVER_SETMASK;

    // Solver flags
    m.attr("SOLVER_FLAG_ALLOW_DOWNGRADE") = SOLVER_FLAG_ALLOW_DOWNGRADE;
    m.attr("SOLVER_FLAG_ALLOW_ARCHCHANGE") = SOLVER_FLAG_ALLOW_ARCHCHANGE;
    m.attr("SOLVER_FLAG_ALLOW_VENDORCHANGE") = SOLVER_FLAG_ALLOW_VENDORCHANGE;
    m.attr("SOLVER_FLAG_ALLOW_UNINSTALL") = SOLVER_FLAG_ALLOW_UNINSTALL;
    m.attr("SOLVER_FLAG_NO_UPDATEPROVIDE") = SOLVER_FLAG_NO_UPDATEPROVIDE;
    m.attr("SOLVER_FLAG_SPLITPROVIDES") = SOLVER_FLAG_SPLITPROVIDES;
    m.attr("SOLVER_FLAG_IGNORE_RECOMMENDED") = SOLVER_FLAG_IGNORE_RECOMMENDED;
    m.attr("SOLVER_FLAG_ADD_ALREADY_RECOMMENDED") = SOLVER_FLAG_ADD_ALREADY_RECOMMENDED;
    m.attr("SOLVER_FLAG_NO_INFARCHCHECK") = SOLVER_FLAG_NO_INFARCHCHECK;
    m.attr("SOLVER_FLAG_ALLOW_NAMECHANGE") = SOLVER_FLAG_ALLOW_NAMECHANGE;
    m.attr("SOLVER_FLAG_KEEP_EXPLICIT_OBSOLETES") = SOLVER_FLAG_KEEP_EXPLICIT_OBSOLETES;
    m.attr("SOLVER_FLAG_BEST_OBEY_POLICY") = SOLVER_FLAG_BEST_OBEY_POLICY;
    m.attr("SOLVER_FLAG_NO_AUTOTARGET") = SOLVER_FLAG_NO_AUTOTARGET;
    m.attr("SOLVER_FLAG_DUP_ALLOW_DOWNGRADE") = SOLVER_FLAG_DUP_ALLOW_DOWNGRADE;
    m.attr("SOLVER_FLAG_DUP_ALLOW_ARCHCHANGE") = SOLVER_FLAG_DUP_ALLOW_ARCHCHANGE;
    m.attr("SOLVER_FLAG_DUP_ALLOW_VENDORCHANGE") = SOLVER_FLAG_DUP_ALLOW_VENDORCHANGE;
    m.attr("SOLVER_FLAG_DUP_ALLOW_NAMECHANGE") = SOLVER_FLAG_DUP_ALLOW_NAMECHANGE;
    m.attr("SOLVER_FLAG_KEEP_ORPHANS") = SOLVER_FLAG_KEEP_ORPHANS;
    m.attr("SOLVER_FLAG_BREAK_ORPHANS") = SOLVER_FLAG_BREAK_ORPHANS;
    m.attr("SOLVER_FLAG_FOCUS_INSTALLED") = SOLVER_FLAG_FOCUS_INSTALLED;
    m.attr("SOLVER_FLAG_YUM_OBSOLETES") = SOLVER_FLAG_YUM_OBSOLETES;
    m.attr("SOLVER_FLAG_NEED_UPDATEPROVIDE") = SOLVER_FLAG_NEED_UPDATEPROVIDE;
    m.attr("SOLVER_FLAG_URPM_REORDER") = SOLVER_FLAG_URPM_REORDER;
    m.attr("SOLVER_FLAG_FOCUS_BEST") = SOLVER_FLAG_FOCUS_BEST;
    m.attr("SOLVER_FLAG_STRONG_RECOMMENDS") = SOLVER_FLAG_STRONG_RECOMMENDS;
    m.attr("SOLVER_FLAG_INSTALL_ALSO_UPDATES") = SOLVER_FLAG_INSTALL_ALSO_UPDATES;
    m.attr("SOLVER_FLAG_ONLY_NAMESPACE_RECOMMENDED") = SOLVER_FLAG_ONLY_NAMESPACE_RECOMMENDED;
}
