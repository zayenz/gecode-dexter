// fzn-pbs.cpp

// Includes
#include <iostream>
#include <fstream>
#include <gecode/flatzinc.hh>
#include <gecode/flatzinc/registry.hh>
#include <gecode/flatzinc/plugin.hh>
#include <gecode/flatzinc/branch.hh>
#include <gecode/search.hh>
#include <gecode/flatzinc/fzn-pbs.hh>
#include <gecode/flatzinc/searchenginebase.hh>
#include <gecode/flatzinc/branchmodifier.hh>

#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <limits>
#include <unordered_set>

using namespace std;
using namespace Gecode;
using namespace Gecode::FlatZinc;

PBSController::PBSController(FlatZinc::FlatZincSpace* fg, const int num_assets, FlatZinc::Printer& p)
    : fg(fg), 
      num_assets(num_assets), 
      assets(num_assets),
      all_best_solutions(num_assets),
      p(p), 
      optimum_found(false), 
      best_sol(nullptr), 
      finished_asset(-1)
    {
    execution_done_wait_started = false;
    running_threads = num_assets;
    // Initialize global_objective given method:
    method = fg->method();
}

PBSController::~PBSController() {
    for (long unsigned int i = 0; i < all_best_solutions.size(); i++){
        if (all_best_solutions[i] != nullptr){
            delete all_best_solutions[i];
            all_best_solutions[i] = nullptr;
        }
    }
}

void PBSController::thread_done() {
    if (running_threads.fetch_sub(1) == 1) {
        execution_done_event.signal();
    }
}

void PBSController::await_runners_completed() {
    execution_done_event.wait();
}

// Print the statistics of the search.
void PBSController::solutionStatistics(BaseAsset* asset, std::ostream& out, Support::Timer& t_total, int finished_asset, bool allAssetStat = false) {
    int n_p = asset->getNP();
    StatusStatistics sstat = asset->getSStat();
    FlatZincSpace *fzs = asset->getFZS();
    BaseEngine* se = asset->getSE();
    Support::Timer* t_solve = asset->getTSolve();
    Gecode::Search::Statistics stat = se->statistics();
    double totalTime = (t_total.stop() / 1000.0);
    double solveTime = (t_solve->stop() / 1000.0);
    double initTime = totalTime - solveTime;

    if (allAssetStat){
        out << std::endl
            << "%%%mzn-stat: initTime=" << initTime
            << std::endl;
        out << "%%%mzn-stat: solveTime=" << solveTime
            << std::endl;
        out << "%%%mzn-stat: finished asset="
            << finished_asset << std::endl;
        out << "%%%mzn-stat: variables="
            << (fzs->getintVarCount() + fzs->getboolVarCount() + fzs->getsetVarCount()) << std::endl
            << "%%%mzn-stat: propagators=" << n_p << std::endl
            << "%%%mzn-stat: propagations=" << sstat.propagate+stat.propagate << std::endl
            << "%%%mzn-stat: nodes=" << stat.node << std::endl
            << "%%%mzn-stat: failures=" << stat.fail << std::endl
            << "%%%mzn-stat: restarts=" << stat.restart << std::endl
            << "%%%mzn-stat: peakDepth=" << stat.depth << std::endl
            << "%%%mzn-stat-end" << std::endl
            << std::endl;

        for (int asset = 0; asset < num_assets; asset++){
            if (asset == finished_asset){
                continue;
            }
            stat = assets[asset]->getSE()->statistics();
            n_p = assets[asset]->getNP();
            out << "%%%mzn-stat: unfinished asset="
                << asset << std::endl;
            out << "%%%mzn-stat: propagators=" << n_p << std::endl
                << "%%%mzn-stat: propagations=" << sstat.propagate+stat.propagate << std::endl
                << "%%%mzn-stat: nodes=" << stat.node << std::endl
                << "%%%mzn-stat: failures=" << stat.fail << std::endl
                << "%%%mzn-stat: restarts=" << stat.restart << std::endl
                << "%%%mzn-stat: peakDepth=" << stat.depth << std::endl
                << "%%%mzn-stat-end" << std::endl
                << std::endl;
        }
    }
    else{
        out << std::endl
            << "%%%mzn-stat: initTime=" << initTime
            << std::endl;
        out << "%%%mzn-stat: solveTime=" << solveTime
            << std::endl;
        out << "%%%mzn-stat: finished asset="
            << finished_asset << std::endl;
        out << "%%%mzn-stat: variables="
            << (fg->getintVarCount() + fg->getboolVarCount() + fg->getsetVarCount()) << std::endl
            << "%%%mzn-stat: propagators=" << n_p << std::endl
            << "%%%mzn-stat: propagations=" << sstat.propagate+stat.propagate << std::endl
            << "%%%mzn-stat: nodes=" << stat.node << std::endl
            << "%%%mzn-stat: failures=" << stat.fail << std::endl
            << "%%%mzn-stat: restarts=" << stat.restart << std::endl
            << "%%%mzn-stat: peakDepth=" << stat.depth << std::endl
            << "%%%mzn-stat-end" << std::endl
            << std::endl;
    }
}

void PBSController::setupPortfolioAssets(int asset, FlatZinc::Printer& p, FlatZincOptions& fopt, std::ostream &out, int num_assets) {
    switch (AssetType(asset))
    {
    case USER:
        // assets[asset] = (std::make_unique<LNSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, FlatZinc::FlatZincSpace::LNSType::AFCLNS, fopt.c_d(), fopt.a_d(), fopt.threads(), RM_CONSTANT, 1.5, 250));
        assets[asset] = (std::make_unique<DFSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, fopt.c_d(), fopt.a_d(), fopt.threads()));
        break;
    case LNS_USER:
        assets[asset] = (std::make_unique<LNSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, FlatZinc::FlatZincSpace::LNSType::RANDOM, fopt.c_d(), fopt.a_d(), fopt.threads(), RM_CONSTANT, 1.5, 250));
        break;
    case PGLNS:
        assets[asset] = (std::make_unique<LNSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, FlatZinc::FlatZincSpace::LNSType::PG, fopt.c_d(), fopt.a_d(), fopt.threads(), RM_CONSTANT, 1.5, 250));
        break;
    case REVPGLNS:
        assets[asset] = (std::make_unique<LNSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, FlatZinc::FlatZincSpace::LNSType::rPG, fopt.c_d(), fopt.a_d(), fopt.threads(), RM_CONSTANT, 1.5, 250));
        break;
    case AFCLNS:
        assets[asset] = (std::make_unique<LNSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, false, FlatZinc::FlatZincSpace::LNSType::AFCLNS, fopt.c_d(), fopt.a_d(), fopt.threads(), RM_CONSTANT, 1.5, 250));
        break;
    case SHAVING:
        assets[asset] = (std::make_unique<ShavingAsset>(*this, fg, p, fopt, out, asset, best_sol, optimum_found, 20, true, new InputOrderVariableSorter()));
        break;
    case USER_OPPOSITE:
        assets[asset] = (std::make_unique<DFSAsset>(*this, fg, fopt, p, out, asset, best_sol, optimum_found, true, fopt.c_d(), fopt.a_d(), fopt.threads()));
        break;
    default:
        break;
    }

    // Set up the printer when reaching the final asset. (To not mess with the shrinking of the printer object.)
    if (asset == num_assets){
        assets[asset]->getFZS()->shrinkArrays(p);
    }
}

// The controller that creates the workers and controls the searches.
void PBSController::controller(std::ostream& out, FlatZincOptions& fopt, Support::Timer& t_total) {
    // Make search space clone-able by calling status on it. If it fails, then the model is unsatisfiable.
    if (fg->status() == SS_FAILED) {
        out << "=====UNSATISFIABLE=====" << std::endl;
        return;
    }

    for (int asset = 0; asset < num_assets; asset++) {
        setupPortfolioAssets(asset, p, fopt, out, num_assets);
        assets[asset]->run();
    }
    await_runners_completed();

    // Print the best or final solution:
    FlatZincSpace* sol = best_sol.load();
    BaseEngine* se = assets[finished_asset]->getSE();
    if (sol) {
        sol->print(out, p);
        out << "----------" << std::endl;
    }

    if (!se->stopped()) {
        if (sol) {
        out << "==========" << std::endl;
        } else {
        out << "=====UNSATISFIABLE=====" << std::endl;
        }
    }
    else if (!sol) {
        out << "=====UNKNOWN=====" << std::endl;
    }

    // If print Statistics:
    if (fopt.mode() == SM_STAT) {
        solutionStatistics(assets[finished_asset].get(), out, t_total, finished_asset, fopt.fullStatistics());
    }
}


// ########################################################################
//                         AssetExecutor below.
// ########################################################################

void AssetExecutor::runSearch(){
    // Start the search timer.
    asset->getTSolve()->start();
    StatusStatistics sstat;
    if (asset->getFZS()->status(sstat) != SS_FAILED) {
        asset->setNP(PropagatorGroup::all.size(*(asset->getFZS())));
        asset->setSStat(sstat);
    }
    else{
        out << "=====UNSATISFIABLE=====" << std::endl;
        control.thread_done();
        return;
    }

    bool printAll = fopt.allSolutions();

    BaseEngine* se = asset->getSE();

    // cerr << "best sol: " << control.best_sol << endl;
    // cerr << "optcon: " << &control.optimum_found << endl;
    // cerr << "optass " << &asset->getopt() << endl;
    // Run the search
    FlatZincSpace* sol = nullptr;
    bool solWasBestSol = false;
    cerr << "starting search for assedt: " << asset_id << endl;
    while (FlatZincSpace* next_sol = se->next()) {
        cerr << "found solution for assedt: " << asset_id << "curropt: " << next_sol->iv[next_sol->optVar()].val() << endl;
        // If last solution was not the current best solution, delete it.
        if (!solWasBestSol && sol != nullptr){
            delete sol;
            sol = nullptr;
        }
        // If one asset finished, stop looking for more solutions. TODO: Make sure that search did not finish due to LNS restart limit reached etc.
        // cerr << "Asset: " << asset_id << " value: " << control.optimum_found.load() << endl;
        // cerr << "Asset: " << asset_id << " value: " << control.optimum_found.load() << endl;

        sol = next_sol;
        solWasBestSol = false;
        int optVar = sol->optVar();
        while(true){
            FlatZincSpace* control_best_sol = control.best_sol.load();

            if (control_best_sol == nullptr){
                    control.best_space_mutex.lock();
                    // Critical Section
                    FlatZincSpace* expected = nullptr;
                    bool success = control.best_sol.compare_exchange_strong(expected, sol);
                    if (success){
                        solWasBestSol = true;
                        control.all_best_solutions.push_back(sol);
                        if (printAll){
                            sol->print(out, p);
                            out << "----------" << std::endl;
                        }
                    }
                    control.best_space_mutex.unlock();
                    
                    if (success){
                        break;
                    }
            }
            else{
                // TODO: Does not handle float yet.
                if (control.method == FlatZincSpace::MAX){
                    if (control_best_sol->iv[optVar].val() < sol->iv[optVar].val()){
                        control.best_space_mutex.lock();
                        // Critical Section
                        FlatZincSpace* expected = control.best_sol.load();
                        if (expected->iv[optVar].val() < sol->iv[optVar].val()){
                            bool success = control.best_sol.compare_exchange_strong(expected, sol);
                            assert(success);
                            solWasBestSol = true;
                            control.all_best_solutions.push_back(sol);
                            if (printAll){
                                sol->print(out, p);
                                out << "----------" << std::endl;
                            }
                        }
                        control.best_space_mutex.unlock();
                    }
                }
                else if (control.method == FlatZincSpace::MIN){
                    if (control_best_sol->iv[optVar].val() > sol->iv[optVar].val()){
                        control.best_space_mutex.lock();
                        // Critical Section
                        FlatZincSpace* expected = control.best_sol.load();
                        if (expected->iv[optVar].val() > sol->iv[optVar].val()){
                            bool success = control.best_sol.compare_exchange_strong(expected, sol);
                            assert(success);
                            solWasBestSol = true;
                            control.all_best_solutions.push_back(sol);
                            if (printAll){
                                sol->print(out, p);
                                out << "----------" << std::endl;
                            }
                        }
                        control.best_space_mutex.unlock();
                    }
                }

                break;
            }
        }
        cerr << "while-loop end: " << asset_id << endl; 
        // Apply the relations to make asset take advantage of shaving.
        long unsigned int size = control.get_forbidden_literals().size();
        cerr << size << endl;
        for (long unsigned int i = asset->getShavingStart(); i < size; i++){
            control.get_forbidden_literals()[i].var.nq(asset->getFZS(), control.get_forbidden_literals()[i].value);
        }
        if (size > 0){
            asset->setShavingStart(size-1);
        }
    }
    asset->getTSolve()->stop();
    cerr << "search over for assedt: " << asset_id << endl;
    // The first asset to finish will be the final best solution.
    if (!control.optimum_found.exchange(true, std::memory_order_release)){
        cerr << "optimum found: " << asset_id << endl;
        control.finished_asset = asset_id;
    }
    else if (!solWasBestSol){
        delete sol;
        sol = nullptr;
    }

    control.thread_done();
}

void AssetExecutor::runShaving(){
    // Cast asset to be a ShavingAsset.
    ShavingAsset* shaving_asset = dynamic_cast<ShavingAsset*>(asset);

    StatusStatistics status_stat;
    CloneStatistics clone_stat;

    bool has_reported_literal = false;
    

    // Shave bounds
    if (shaving_asset->doBoundsShaving()) {
        shaving_asset->run_shaving_pass(control, status_stat, clone_stat, has_reported_literal, [](VarDescription& vd, FlatZincSpace* s) {
            return vd.bounds_literals(s);
        });
    }
    else {
        shaving_asset->run_shaving_pass(control, status_stat, clone_stat, has_reported_literal, [shaving_asset](VarDescription& vd, FlatZincSpace* s) {
            if (vd.size(s) > static_cast<unsigned int>(shaving_asset->getMaxDomShavingSize())) {
                return std::vector<Literal>{};
            }
            return vd.domain_literals(s);
        });
    }

    // TODO: Handle statistics and report to controller?

    // if (has_reported_literal) {
    //     return AssetResult(AssetResultCode::CanBeRestarted);
    // } else {
    //     return AssetResult(AssetResultCode::Done);
    // }
}

// ########################################################################
//                         Assets Below.
// ########################################################################

void DFSAsset::setupAsset(){
    // Set up the portfolio assets.
    fzs = static_cast<FlatZinc::FlatZincSpace*>(fg->clone());
    // Set the solve annotations for the asset, as it does not follow from the clone.
    fzs->setSolveAnnotations(fg->solveAnnotations());
    // Set the shared current best solutions between assets for each asset.
    fzs->pbs_current_best_sol = &best_sol;
    // Copy iv,bv,sv_introduced vector from fg, as it does not follow the cloning process.
    fzs->iv_introduced = fg->iv_introduced;
    fzs->bv_introduced = fg->bv_introduced;
    fzs->sv_introduced = fg->sv_introduced;

    Search::Options search_options;
    search_options.stop = Driver::PBSCombinedStop::create(fopt.node(), fopt.fail(), fopt.time(), 0, true, optimum_found);
    search_options.c_d = c_d;
    search_options.a_d = a_d;

    #ifdef GECODE_HAS_CPPROFILER
        if (fopt.profiler_port()) {
            FlatZincGetInfo* getInfo = nullptr;

            if (fopt.profiler_info()) getInfo = new FlatZincGetInfo(p);
            
            search_options.tracer = new CPProfilerSearchTracer(fopt.profiler_id(), fopt.name(), fopt.profiler_port(), getInfo);
        }

    #endif

    #ifdef GECODE_HAS_FLOAT_VARS
        fzs->step = fopt.step();
    #endif

    search_options.threads = threads;
    search_options.nogoods_limit = fopt.nogoods() ? fopt.nogoods_limit() : 0;

    if (fopt.interrupt()) Driver::PBSCombinedStop::installCtrlHandler(true);

    // If not RBS but asset is to use it:
    if (fopt.restart() != RM_NONE){
        fopt.restart(RM_NONE);
    }
    search_options.cutoff = new Search::CutoffAppend(new Search::CutoffConstant(0), 1, Driver::createCutoff(fopt));
    // Create the branchers. (Needs to be here due to non-rbs options and assets that may utilize rbs.)
    fzs->createBranchers(p, fzs->solveAnnotations(), fopt, false, bm, std::cerr);
    
    se = new BABEngine(fzs, search_options);
}
void LNSAsset::setupAsset(){
    // Set up the portfolio assets.
    fzs = static_cast<FlatZinc::FlatZincSpace*>(fg->clone());
    // Set the solve annotations for the asset, as it does not follow from the clone.
    fzs->setSolveAnnotations(fg->solveAnnotations());
    // Set the shared current best solutions between assets for each asset.
    fzs->pbs_current_best_sol = &best_sol;
    // Copy iv,bv,sv_introduced vector from fg, as it does not follow the cloning process.
    fzs->iv_introduced = fg->iv_introduced;
    fzs->bv_introduced = fg->bv_introduced;
    fzs->sv_introduced = fg->sv_introduced;

    Search::Options search_options;
    search_options.stop = Driver::PBSCombinedStop::create(fopt.node(), fopt.fail(), fopt.time(), fopt.restart_limit(), true, optimum_found);
    search_options.c_d = c_d;
    search_options.a_d = a_d;

    #ifdef GECODE_HAS_CPPROFILER
        if (fopt.profiler_port()) {
            FlatZincGetInfo* getInfo = nullptr;

            if (fopt.profiler_info()) getInfo = new FlatZincGetInfo(p);
            
            search_options.tracer = new CPProfilerSearchTracer(fopt.profiler_id(), fopt.name(), fopt.profiler_port(), getInfo);
        }

    #endif

    #ifdef GECODE_HAS_FLOAT_VARS
        fzs->step = fopt.step();
    #endif

    search_options.threads = threads;
    search_options.nogoods_limit = fopt.nogoods() ? fopt.nogoods_limit() : 0;

    fzs->setLNSType(lns_type);

    if (fopt.interrupt()) Driver::PBSCombinedStop::installCtrlHandler(true);

    // If not RBS but asset is to use it:
    if (fopt.restart() == RM_NONE){
        fopt.restart(mode);
        fopt.restart_base(restart_base);
        fopt.restart_scale(restart_scale);
        search_options.cutoff = new Search::CutoffAppend(new Search::CutoffConstant(0), 1, Driver::createCutoff(fopt));
        // Create the branchers. (Needs to be here due to non-rbs options and assets that may utilize rbs.)
        fzs->createBranchers(p, fzs->solveAnnotations(), fopt, false, bm, std::cerr);
        fopt.restart(RM_NONE);
    }
    else{
        search_options.cutoff = new Search::CutoffAppend(new Search::CutoffConstant(0), 1, Driver::createCutoff(fopt));
        fzs->createBranchers(p, fzs->solveAnnotations(), fopt, false, bm, std::cerr);
    }
    
    se = new RBSEngine(fzs, search_options);
}

void ShavingAsset::setupAsset(){
    for (int i = 0; i < root->iv.size(); i++) {
        if (root->iv[i].assigned()) {
            continue;
        }
        variables.push_back(VarDescription(VarType::Int, FlatZincVarArray::iv, i));
    }
    for (int i = 0; i < root->bv.size(); i++) {
        if (root->bv[i].assigned()) {
            continue;
        }
        variables.push_back(VarDescription(VarType::Bool, FlatZincVarArray::bv, i));
    }
}

void ShavingAsset::run_shaving_pass(PBSController& control, StatusStatistics status_stat, CloneStatistics clone_stat, bool& has_reported_literal, const std::function<std::vector<Literal> (VarDescription&, FlatZincSpace*)> literal_extractor) {
    std::vector queue(variables);
    sorter->sort_variables(queue, root);
    while (!queue.empty()) {
        if (control.optimum_found.load()) {
            cerr << "shaving out" << endl;
            control.thread_done();
            return;
        }
        auto vd = queue.back();
        queue.pop_back();

        for (auto literal : literal_extractor(vd, root)) {
            if (control.optimum_found.load()) {
                cerr << "shaving out" << endl;
                control.thread_done();
                return;
            }
            auto clone = dynamic_cast<FlatZincSpace*>(root->clone(clone_stat));
            literal.var.eq(clone, literal.value);
            auto status = clone->status(status_stat);
            delete clone;
            if (status == SS_FAILED) {
                control.report_forbidden_literal(literal);
                has_reported_literal = true;
                literal.var.nq(root, literal.value);
                auto root_status = root->status(status_stat);
                if (root_status == SS_FAILED) {
                    control.thread_done();
                    return;
                }
            }
        }

        sorter->sort_variables(queue, root);
    }
    cerr << "shaving out1111" << endl;
    control.thread_done();
}