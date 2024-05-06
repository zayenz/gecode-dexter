// fzn-pbs.hh

#ifndef FZN_PBS_HH
#define FZN_PBS_HH

// Includes
#include <gecode/flatzinc.hh>
#include <gecode/flatzinc/branch.hh>
#include <gecode/search.hh>
#include <gecode/flatzinc/searchenginebase.hh>

#include <memory>
#include <vector>
#include <array>
#include <string>
#include <sstream>
#include <limits>
#include <unordered_set>

using namespace std;
using namespace Gecode;
using namespace Gecode::FlatZinc;

class PBSController;  // Declaration
class BaseAsset;  // Declaration

enum class VarType { Int, Bool, };

enum class FlatZincVarArray { iv, iv_aux, bv, bv_aux, };

struct VarDescription {
    VarType type;
    FlatZincVarArray array;
    int position;

    VarDescription(VarType type, FlatZincVarArray array, int position) : type(type), array(array), position(position) {}

    Gecode::IntVar int_variable(FlatZincSpace* s) const {
        assert(type == VarType::Int);
        switch (array) {
            default:
                GECODE_NEVER;
            case FlatZincVarArray::iv:
                return s->iv[position];
            case FlatZincVarArray::iv_aux:
                return s->iv_aux[position];
        }
    }

    Gecode::BoolVar bool_variable(FlatZincSpace* s) const {
        assert(type == VarType::Bool);
        switch (array) {
            default:
                GECODE_NEVER;
            case FlatZincVarArray::bv:
                return s->bv[position];
            case FlatZincVarArray::bv_aux:
                return s->bv_aux[position];
        }
    }

    bool is_assigned(FlatZincSpace* s) const {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                return int_variable(s).assigned();
            case VarType::Bool:
                return bool_variable(s).assigned();
        }
    }

    unsigned int size(FlatZincSpace* s) const {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                return int_variable(s).size();
            case VarType::Bool:
                return bool_variable(s).size();
        }
    }

    int min(FlatZincSpace* s) const {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                return int_variable(s).min();
            case VarType::Bool:
                return bool_variable(s).min();
        }
    }

    int max(FlatZincSpace* s) const {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                return int_variable(s).max();
            case VarType::Bool:
                return bool_variable(s).max();
        }
    }

    double afc(FlatZincSpace* s) const {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                return int_variable(s).afc();
            case VarType::Bool:
                return bool_variable(s).afc();
        }
    }

    auto bounds_literals(FlatZincSpace* s) const;
    auto domain_literals(FlatZincSpace* s) const;

    void eq(FlatZincSpace* s, int value) {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                rel(*s, int_variable(s), IRT_EQ, value);
                break;
            case VarType::Bool:
                rel(*s, bool_variable(s), IRT_EQ, value);
                break;
        }
    }

    void nq(FlatZincSpace* s, int value) {
        switch (type) {
            default:
                GECODE_NEVER;
            case VarType::Int:
                rel(*s, int_variable(s), IRT_NQ, value);
            break;
            case VarType::Bool:
                rel(*s, bool_variable(s), IRT_NQ, value);
            break;
        }
    }
};

struct Literal {
    VarDescription var;
    int value;

    Literal(const VarDescription& var, int value)
        : var(var),
          value(value) {
    }

};

inline auto VarDescription::bounds_literals(FlatZincSpace* s) const {
    return std::vector{
        Literal(*this, min(s)),
        Literal(*this, max(s)),
    };
}

inline auto VarDescription::domain_literals(FlatZincSpace* s) const {
    switch (type) {
        default:
            GECODE_NEVER;
        case VarType::Int: {
            std::vector<Literal> result;
            IntVar v = int_variable(s);
            result.reserve(v.size());
            auto values = IntVarValues(v);
            while (values()) {
                result.emplace_back(Literal(*this, values.val()));
                ++values;
            }
            return result;
        }
        case VarType::Bool:
            return bounds_literals(s);
    }
}

class VariableSorter {
public:
    virtual ~VariableSorter() = default;

    /// Sort the variables, setting the most interesting variable last
    virtual void sort_variables(std::vector<VarDescription>& variables, FlatZincSpace* root) = 0;
};

class InputOrderVariableSorter : public VariableSorter {
public:
    void sort_variables(std::vector<VarDescription>& /*variables*/, FlatZincSpace* /*root*/) override {
        // No rearrangement
    }
};

class SmallestSizeVariableSorter : public VariableSorter {
public:
    void sort_variables(std::vector<VarDescription>&variables, FlatZincSpace* root) override {
        // Sort with smallest variables last
        std::sort(variables.begin(), variables.end(),
                  [root](const VarDescription&a, const VarDescription&b) {
                      return a.size(root) > b.size(root);
                  });
    }
};

class LargestAFCVariableSorter : public VariableSorter {
public:
    void sort_variables(std::vector<VarDescription>&variables, FlatZincSpace* root) override {
        // Sort with largest AFC last
        std::sort(variables.begin(), variables.end(),
                  [root](const VarDescription&a, const VarDescription&b) {
                      return a.size(root) < b.size(root);
                  });
    }
};

class AssetExecutor : public Gecode::Support::Runnable {
    /// The common controller for running tests
    PBSController& control;
    /// The running asset.
    BaseAsset* asset;
    // The output stream.
    std::ostream& out;
    // The options for the FlatZinc space and search.
    FlatZincOptions& fopt;
    // Printer (Stores the output variables)
    FlatZinc::Printer& p;
    // The asset for this search.
    int asset_id;
    // If run search or not.
    bool do_search;
    // Different runs depending on asset.
    void runSearch();
    void runShaving();

public:
    // Constructor
    AssetExecutor(PBSController& control, BaseAsset* asset, std::ostream& out, FlatZincOptions& fopt, FlatZinc::Printer& p, int asset_id, bool do_search)
    : control(control), asset(asset), out(out), fopt(fopt), p(p), asset_id(asset_id), do_search(do_search) {}
    // Run the search.
    void run(void){do_search ? runSearch() : runShaving();};
};

class BaseAsset {
    public:
        virtual ~BaseAsset() = default;
        virtual void setupAsset() = 0;
        virtual void run() = 0;
        virtual FlatZincSpace* getFZS() = 0;
        virtual BaseEngine* getSE() = 0;
        virtual StatusStatistics getSStat() = 0;
        virtual int getNP() const = 0;
        virtual long unsigned int getShavingStart() const = 0;
        virtual double getSolveTime() const = 0;
        virtual FlatZinc::FlatZincSpace::LNSType getLNSType() const = 0;
        virtual string getAssetTypeStr() const = 0;
        

        virtual void setNP(int n_p) = 0;
        virtual void setSStat(StatusStatistics sstat) = 0;
        virtual void setShavingStart(long unsigned int start) = 0;
        virtual void setAssetTypeStr(string type) = 0;

        virtual void increaseSolveTime(double time) = 0;
};

class DFSAsset : public BaseAsset {
    public:
        DFSAsset(PBSController& control, FlatZincSpace* fg, FlatZincOptions& fopt, FlatZinc::Printer& p, std::ostream &out, unsigned int asset_id, bool opposite_branching, bool initial_branch_by_afc, unsigned int c_d, unsigned int a_d, double threads)
        : control(control), fg(fg), fopt(fopt), p(p), c_d(c_d), a_d(a_d), threads(threads), bm(opposite_branching, initial_branch_by_afc), executor(new AssetExecutor(control, this, out, fopt, p, asset_id, true)), shaving_start(0), solve_time(0) {setupAsset();};

        ~DFSAsset() override {
            delete se; se = nullptr;
            delete fzs; fzs = nullptr;
            // delete executor; executor = nullptr;
        };

        void setupAsset() override;
        void run() override {Gecode::Support::Thread::run(executor);};

        FlatZincSpace* getFZS() override { return fzs; }
        BaseEngine* getSE() override { return se; }
        StatusStatistics getSStat() override { return sstat; }
        int getNP() const override { return n_p; }
        long unsigned int getShavingStart() const override { return shaving_start; }
        double getSolveTime() const override { return solve_time; }
        FlatZinc::FlatZincSpace::LNSType getLNSType() const override { return FlatZinc::FlatZincSpace::LNSType::NONE; }
        string getAssetTypeStr() const override { return assetstr; }
        
        void setNP(int n_p) override { this->n_p = n_p; }
        void setSStat(StatusStatistics sstat) override { this->sstat = sstat; }
        void setShavingStart(long unsigned int start) override { shaving_start = start; }
        void setAssetTypeStr(string type) override { assetstr = type; }

        void increaseSolveTime(double time) override {solve_time += time;};
        
        PBSController& control;

    private:
        FlatZincSpace* fzs;
        BABEngine* se;
        StatusStatistics sstat;
        int n_p;
        FlatZincSpace* fg;
        FlatZincOptions& fopt;
        FlatZinc::Printer& p;
        unsigned int c_d;
        unsigned int a_d;
        double threads;
        BranchModifier bm;
        AssetExecutor* executor;
        long unsigned int shaving_start;
        double solve_time;
        string assetstr;
};

class LNSAsset : public BaseAsset {
    public:
        LNSAsset(PBSController& control, FlatZincSpace* fg, FlatZincOptions& fopt, FlatZinc::Printer& p, std::ostream &out, unsigned int asset_id, bool opposite_branching, bool initial_branch_by_afc, FlatZinc::FlatZincSpace::LNSType lns_type, unsigned int c_d, unsigned int a_d,
                     double threads, RestartMode mode, double restart_base, unsigned int restart_scale) 
                    : control(control), fg(fg), fopt(fopt), p(p), c_d(c_d), a_d(a_d), threads(threads), bm(opposite_branching, initial_branch_by_afc), mode(mode), restart_base(restart_base), 
                      restart_scale(restart_scale), lns_type(lns_type), executor(new AssetExecutor(control, this, out, fopt, p, asset_id, true)), shaving_start(0), solve_time(0) {setupAsset();};
        ~LNSAsset() override {
            delete se; se = nullptr;
            delete fzs->ciglns_info; fzs->ciglns_info = nullptr;
            delete fzs; fzs = nullptr;
            // delete executor; executor = nullptr;
        };

        void setupAsset() override;
        void run() override {Gecode::Support::Thread::run(executor);};

        FlatZincSpace* getFZS() override { return fzs; }
        BaseEngine* getSE() override { return se; }
        StatusStatistics getSStat() override { return sstat; }
        int getNP() const override { return n_p; }
        long unsigned int getShavingStart() const override { return shaving_start; }
        double getSolveTime() const override { return solve_time; }
        FlatZinc::FlatZincSpace::LNSType getLNSType() const override { return lns_type; }
        string getAssetTypeStr() const override { return assetstr; }

        void setNP(int n_p) override { n_p = n_p; }
        void setSStat(StatusStatistics sstat) override { sstat = sstat; }
        void setShavingStart(long unsigned int start) override { shaving_start = start; }
        void setAssetTypeStr(string type) override { assetstr = type; }

        void increaseSolveTime(double time) override {solve_time += time;};
        

        PBSController& control;

    private:
        FlatZincSpace* fzs;
        RBSEngine* se;
        StatusStatistics sstat;
        int n_p;
        FlatZincSpace* fg;
        FlatZincOptions& fopt;
        FlatZinc::Printer& p;
        unsigned int c_d;
        unsigned int a_d;
        double threads;
        BranchModifier bm;
        RestartMode mode;
        double restart_base;
        unsigned int restart_scale;
        FlatZinc::FlatZincSpace::LNSType lns_type;
        AssetExecutor* executor;
        long unsigned int shaving_start;
        double solve_time;
        string assetstr;
};

class RRLNSAsset : public BaseAsset {
    public:
        RRLNSAsset(PBSController& control, FlatZincSpace* fg, FlatZincOptions& fopt, FlatZinc::Printer& p, std::ostream &out, unsigned int asset_id, unsigned int c_d, unsigned int a_d, double threads)
        : best_asset(nullptr), control(control), fg(fg), fopt(fopt), p(p), out(out), c_d(c_d), a_d(a_d), threads(threads), asset_id(asset_id)  {setupAsset();};
        ~RRLNSAsset() override {};
        void setupAsset() override;
        void run() override;

        FlatZincSpace* getFZS() override { return best_asset->getFZS(); }
        BaseEngine* getSE() override { return best_asset->getSE(); }
        StatusStatistics getSStat() override { return best_asset->getSStat(); }
        int getNP() const override { return best_asset->getNP(); }
        double getSolveTime() const override { return best_asset->getSolveTime(); }
        long unsigned int getShavingStart() const override { return -1; }
        FlatZinc::FlatZincSpace::LNSType getLNSType() const override { return best_asset->getLNSType(); }
        string getAssetTypeStr() const override { return best_asset->getAssetTypeStr(); }

        void setNP(int n_p) override { best_asset->setNP(n_p); }
        void setSStat(StatusStatistics sstat) override { best_asset->setSStat(sstat); }
        void setShavingStart(long unsigned int /*start*/) override {}
        void setAssetTypeStr(string type) override { if (best_asset != nullptr) best_asset->setAssetTypeStr(type); }

        void increaseSolveTime(double /*time*/) override {};

        std::unique_ptr<BaseAsset> best_asset;

    private:
        PBSController& control;
        StatusStatistics sstat;
        int n_p;
        FlatZincSpace* fg;
        FlatZincOptions& fopt;
        FlatZinc::Printer& p;
        std::ostream &out;
        unsigned int c_d;
        unsigned int a_d;
        double threads;
        unsigned int asset_id;
        std::vector<std::unique_ptr<BaseAsset>> round_robin_assets;
        string assetstr;
};

class ShavingAsset : public BaseAsset {
    public:
        ShavingAsset(PBSController& control, FlatZincSpace* fg, Gecode::FlatZinc::Printer &p, FlatZincOptions& fopt, std::ostream &out, unsigned int asset_id, int max_dom_shaving_size, bool do_bounds_shaving, VariableSorter* sorter) 
        : control(control), root(fg), fopt(fopt), executor(new AssetExecutor(control, this, out, fopt, p, asset_id, false)), max_dom_shaving_size(max_dom_shaving_size), do_bounds_shaving(do_bounds_shaving), sorter(sorter)
        {
            std::reverse(variables.begin(), variables.end()); setupAsset();
        };
        ~ShavingAsset() override {delete sorter; sorter = nullptr; delete executor; executor = nullptr;};

        void setupAsset() override;
        void run() override {Gecode::Support::Thread::run(executor);};
        void run_shaving_pass(PBSController& control, StatusStatistics status_stat, CloneStatistics clone_stat, bool& has_reported_literal, const std::function<std::vector<Literal> (VarDescription&, FlatZincSpace*)> literal_extractor);
        
        FlatZincSpace* getFZS() override { return root; }
        BaseEngine* getSE() override { return nullptr; }
        StatusStatistics getSStat() override { return sstat; }
        int getNP() const override { return n_p; }
        bool doBoundsShaving() const { return do_bounds_shaving; }
        int getMaxDomShavingSize() const { return max_dom_shaving_size; }
        long unsigned int getShavingStart() const override { return 0; }
        double getSolveTime() const override { return 0; }
        FlatZinc::FlatZincSpace::LNSType getLNSType() const override { return FlatZinc::FlatZincSpace::LNSType::NONE; }
        string getAssetTypeStr() const override { return assetstr; }

        void setNP(int n_p) override { n_p = n_p; }
        void setSStat(StatusStatistics sstat) override { sstat = sstat; }
        void setShavingStart(long unsigned int /*start*/) override {}
        void setAssetTypeStr(string type) override { assetstr = type; }

        void increaseSolveTime(double /*time*/) override {};

        PBSController& control;

    private:
        FlatZincSpace* root;
        StatusStatistics sstat;
        int n_p;
        FlatZincOptions& fopt;
        AssetExecutor* executor;

        std::vector<VarDescription> variables;
        int max_dom_shaving_size;
        bool do_bounds_shaving;
        VariableSorter* sorter;
        string assetstr;
};

class PBSController {
public:
    enum AssetType {
        USER, //< First asset is the user asset.
        LNS_USER, //< Second asset is the user asset with LNS.
        PGLNS, //< Propagation guided LNS.

        SHAVING, //< Shaving asset.

        REVPGLNS, //< Reverse propagation guided LNS.
        AFCLNS, //< AFC guided LNS.
        
        USER_OPPOSITE  //< The user asset with opposite branching.
    };
    // Methods
    PBSController(FlatZinc::FlatZincSpace* fg, const int num_assets, Printer& p); // constructor
    ~PBSController(); // destructor
    void controller(std::ostream& out, FlatZincOptions& fopt, Support::Timer& t_total);
    // Emplace forbidden literal.
    void report_forbidden_literal(Literal forbidden) { forbidden_literals.emplace_back(forbidden); }
    std::vector<Literal> get_forbidden_literals() { return forbidden_literals; }
    // Signals that a search for a thread is finished.
    void thread_done();

    // Variables
    // Intial search space.
    FlatZinc::FlatZincSpace* fg; 
    // The number of assets.
    const int num_assets;
    // Each asset controller by the controller.
    std::vector<std::unique_ptr<BaseAsset>> assets;
    // The best solutions found during search.
    std::vector<FlatZincSpace*> all_best_solutions; 
    // The printer for each asset.
    FlatZinc::Printer& p;
    /// Flag indicating that the final best solution has been found.
    std::atomic<bool> optimum_found;
    // The best solution found given objective value.
    std::atomic<FlatZincSpace*> best_sol;
    // The current method.
    FlatZincSpace::Meth method;
    // A mutex lock for updating best space.
    std::mutex best_space_mutex;
    // The asset that finished the search and found the solution.
    int finished_asset;

private:
    // Waits for all threads to be done.
    void await_runners_completed();
    // Sets up the asset used by the portfolio.
    void setupPortfolioAssets(int asset, FlatZinc::Printer& p, FlatZincOptions& fopt, std::ostream &out, int num_assets);
    // Gives the statistics of the solution. (TODO: Make it possible to output from all engines and/or spaces)
    void solutionStatistics(BaseAsset* asset, std::ostream& out, Support::Timer& t_total, int finished_asset, bool allAssetStat);

    // Variables
    /// Flag indicating some thread is waiting on the execution to be done.
    std::atomic<bool> execution_done_wait_started;
    /// Event for signaling that execution is done.
    Gecode::Support::Event execution_done_event;
    /// The number of test runners that are to be set up.
    std::atomic<int> running_threads;
    // Literals that are forbidden in the search.
    std::vector<Literal> forbidden_literals;
};

// }} 
#endif // FZN_PBS_HH