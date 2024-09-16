//
// pdaggerq - A code for bringing strings of creation / annihilation operators to normal order.
// Filename: consolidate.cc
// Copyright (C) 2020 A. Eugene DePrince III
//
// Author: A. Eugene DePrince III <adeprince@fsu.edu>
// Maintainer: DePrince group
//
// This file is part of the pdaggerq package.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//

#include <memory>

#include "../include/pq_graph.h"
#include "iostream"

// include omp only if defined
#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#endif

using std::ostream, std::string, std::vector, std::map, std::unordered_map, std::shared_ptr, std::make_shared,
        std::set, std::unordered_set, std::pair, std::make_pair, std::to_string, std::invalid_argument,
        std::stringstream, std::cout, std::endl, std::flush, std::max, std::min;

using namespace pdaggerq;


void PQGraph::make_all_links(bool recompute) {

    if (recompute)
        all_links_.clear(); // clear all prior candidates

    linkage_set candidate_linkages; // set of linkages
    for (auto &[eq_name, equation]: equations_) {
        // get all linkages of equation and add to candidates
        all_links_ += equation.make_all_links(recompute);
    }

    // clear history of all linkages
    for (auto &linkage: all_links_)
        linkage->forget();

}

Term& PQGraph::add_tmp(const ConstLinkagePtr& precon, Equation &equation, double coeff) {
    // make term with tmp
    equation.terms().insert(equation.end(), Term(precon, coeff));
    return equation.terms().back();
}

void PQGraph::substitute(bool ignore_declarations, bool format_sigma, bool only_scalars) {

    // begin timings
    total_timer.start();

    // reorder if not already reordered
    if (!is_reordered_) reorder();

    update_timer.start();

    /// ensure necessary equations exist
    bool missing_temp_eq = equations_.find("temp") == equations_.end();
    bool missing_reuse_eq = equations_.find("reused") == equations_.end();
    bool missing_scalar_eq = equations_.find("scalar") == equations_.end();

    vector<string> missing_eqs;
    if (missing_temp_eq) missing_eqs.emplace_back("temp");
    if (missing_reuse_eq) missing_eqs.emplace_back("reused");
    if (missing_scalar_eq) missing_eqs.emplace_back("scalar");

    // add missing equations
    for (const auto &missing: missing_eqs) {
        equations_.emplace(missing, Equation(make_shared<Vertex>(missing), {}));
        equations_[missing].is_temp_equation_ = true; // do not allow substitution of tmp declarations
    }

    /// generate all possible linkages from all arrangements


    cout << "Generating all possible linkages..." << flush;

    size_t org_max_depth = Term::max_depth_;

    if (batched_)
        Term::max_depth_ = 1; // set max depth to 2 for initial linkage generation
    size_t current_depth = 1; // current depth of linkages

    make_all_links(true); // generate all possible linkages
    cout << " Done" << endl;

    size_t num_terms = 0;
    for (const auto &eq_pair: equations_) {
        const Equation &equation = eq_pair.second;
        num_terms += equation.size();
    }

    size_t num_contract = flop_map_.total();

    cout << " ==> Substituting linkages into all equations <==" << endl;
    cout << "     Total number of terms: " << num_terms << endl;
    cout << "        Total contractions: " << flop_map_.total() << endl;
    cout << "     Use batched algorithm: " << (batched_ ? "yes" : "no") << endl;
    if (batched_)
        cout << "                Batch size: " << ((long) batch_size_ == -1 ? "no limit" : to_string(batch_size_))
             << endl;
    cout << "         Max linkage depth: " << ((long) Term::max_depth_ == -1 ? "no limit" : to_string(Term::max_depth_))
         << endl;
    cout << "    Possible intermediates: " << all_links_.size() << endl;
    cout << "    Number of threads used: " << nthreads_ << endl;
    cout << " ====================================================" << endl << endl;

    // give user a warning if the number of possible linkages is large
    // suggest using the batch algorithm, making the max linkage smaller, or increasing number of threads
    if (all_links_.size() * num_contract > 1000 * 10000) {
        cout << "WARNING: There are a large number of contractions and candidate intermediates." << endl;
        cout << "         This may take a long time to run." << endl;
        cout
                << "         Consider increasing the number of threads, making the max depth smaller, or using the batch algorithm."
                << endl;
        cout << endl; //185
    }

    static size_t total_num_merged = 0;
    size_t num_merged = merge_terms();
    total_num_merged += num_merged;

    // initialize best flop map for all equations
    collect_scaling(true);

    // set of linkages to ignore
    linkage_set ignore_linkages(all_links_.size());

    // add all saved linkages to ignore linkages
    for (const auto &[type, linkages]: saved_linkages_) {
        ignore_linkages += linkages;
    }

    // get linkages with the highest scaling (use all linkages for first iteration, regardless of batched)
    // this helps remove impossible linkages from the set without regenerating all linkages as often
    linkage_set test_linkages = all_links_;

    update_timer.stop();

    bool first_pass = true;
    scaling_map best_flop_map = flop_map_;
    static size_t totalSubs = 0;
    string temp_type = format_sigma ? "reused" : "temp"; // type of temporary to substitute
    temp_type = only_scalars ? "scalar" : temp_type; // type of equation to substitute into

    bool makeSub; // flag to make a substitution
    bool found_any = false; // flag to check if we found any linkages
    while ((!test_linkages.empty() || first_pass) && temp_counts_[temp_type] < max_temps_) {
        substitute_timer.start();

        makeSub = false; // reset flag
        bool allow_equality = true; // flag to allow equality in flop map
        size_t n_linkages = test_linkages.size(); // get number of linkages
        LinkagePtr link_to_sub; // best linkage to substitute

        // populate with pairs of flop maps with linkage for each equation
        vector<pair<scaling_map, LinkagePtr>> test_data(n_linkages);


        // print ratio for showing progress
        size_t print_ratio = n_linkages / 20;
        bool print_progress = n_linkages > 2000;

        if (print_progress)
            cout << "PROGRESS:" << endl;

        /**
         * Iterate over all linkages in parallel and test if they can be substituted into the equations.
         * If they can, save the flop map for each equation.
         * If the flop map is better than the current best flop map, save the linkage.
         */
#pragma omp parallel for schedule(guided) default(none) shared(test_linkages, test_data, \
            ignore_linkages, equations_, stdout) firstprivate(n_linkages, temp_counts_, temp_type, allow_equality, \
            format_sigma, print_ratio, print_progress, only_scalars, ignore_declarations)
        for (int i = 0; i < n_linkages; ++i) {

            // copy linkage
            LinkagePtr linkage = as_link(test_linkages[i]->shallow());
            bool is_scalar = linkage->is_scalar(); // check if linkage is a scalar
            string eq_type = is_scalar ? "scalar" : temp_type; // get equation type
            if (is_scalar) {
                // if no scalars are allowed, skip this linkage
                if (Equation::no_scalars_) {
                    linkage->forget(); // clear linkage history
                    ignore_linkages.insert(linkage); // add linkage to ignore linkages
                    continue;
                }
            }

            if ((format_sigma && linkage->is_sigma_) || (only_scalars && !is_scalar)) {
                // when formatting for sigma vectors,
                // we only keep linkages without a sigma vector and are not scalars
                linkage->forget(); // clear linkage history
                ignore_linkages.insert(linkage);
                continue;
            }
            linkage->reused_ = format_sigma;

            // set id of linkage
            long temp_id = temp_counts_[eq_type] + 1; // get number of temps
            linkage->id() = temp_id;

            scaling_map test_flop_map; // flop map for test equation
            size_t numSubs = 0; // number of substitutions made
            for (auto &[eq_name, equation]: equations_) { // iterate over equations

                // skip scalar equations
                if (eq_name == "scalar") continue;


                // if the substitution is possible and beneficial, collect the flop map for the test equation
                numSubs += equation.test_substitute(linkage, test_flop_map,
                                                    allow_equality || is_scalar || format_sigma);
            }

            // add to test scalings if we found a tmp that occurs in more than one term
            // or that occurs at least once and can be reused / is a scalar

            // include declaration for scaling?
            bool keep_declaration = !is_scalar && !format_sigma;
            keep_declaration &= !ignore_declarations;

            // test if we made a valid substitution
            if (numSubs > 0) {

                if (keep_declaration) {
                    // make term of tmp declaration
                    Term precon_term = Term(linkage, 1.0);
                    precon_term.compute_scaling();
                    // add scaling of declaration term to the test flop map if we are keeping the declaration
                    test_flop_map += precon_term.flop_map();
                }

                // set any negative values to zero
                test_flop_map.all_positive();

                // save this test flop map and linkage for serial testing
                test_data[i] = make_pair(test_flop_map, linkage);

            } else { // if we didn't make a substitution, add linkage to ignore linkages
                linkage->forget(); // clear linkage history
                ignore_linkages.insert(linkage);
            }

            if (print_progress && i % print_ratio == 0) {
                printf("  %2.1lf%%", (double) i / (double) n_linkages * 100);
                std::fflush(stdout);
            }

        } // end iterations over all linkages
        if (print_progress) std::cout << "  Done" << std::endl << std::endl;



        /**
         * Iterate over all test scalings, remove incompatible ones, and sort them
         */

        std::multimap<scaling_map, LinkagePtr> sorted_test_data;
        for (auto &[test_flop_map, test_linkage]: test_data) {

            // skip empty linkages
            if (test_linkage == nullptr) continue;
            if (test_linkage->empty()) continue;
            test_linkage->forget(); // clear linkage history

            if (test_flop_map > flop_map_) {
                // remove the linkage completely if the scaling only got worse
                ignore_linkages.insert(test_linkage);
                continue;
            }

            bool is_scalar = test_linkage->is_scalar(); // check if linkage is a scalar

            // test if this is the best flop map seen
            int comparison = test_flop_map.compare(flop_map_);
            bool is_equiv = comparison == scaling_map::this_same;
            bool keep = comparison == scaling_map::this_better;

            // if we haven't made a substitution yet and this is either a
            // scalar or a sigma vector, keep it
            if (format_sigma || (is_scalar && !Equation::no_scalars_)) keep = true;

            // if the scaling is the same and it is allowed, set keep to true
            if (!keep && is_equiv && allow_equality) keep = true;


            if (keep) {
                sorted_test_data.insert(make_pair(test_flop_map, test_linkage));
            } else {
                ignore_linkages.insert(test_linkage); // add linkage to ignore linkages
            }
        }
        substitute_timer.stop(); // stop timer for substitution

        makeSub = !sorted_test_data.empty();
        if (makeSub) {

            /**
             * we found substitutions, so we need to update the equations.
             * we need to:
             *     actually substitute the linkage in all equations
             *     store the declarations for the tmps.
             *     update the flop map and memory map.
             *     update the total number of substitutions.
             *     update the total number of terms.
             *     generate a new test set without this linkage.
             * we do this for every linkage that would improve the scaling in order of the linkage that
             * improves the scaling the most.
             */

            update_timer.start();

            size_t batch_count = 0;
            for (const auto &[found_flop, found_linkage]: sorted_test_data) {

                substitute_timer.start();

                link_to_sub = found_linkage;

                // check if link is a scalar
                bool is_scalar = link_to_sub->is_scalar();

                // get number of temps for this type
                string eq_type = is_scalar ? "scalar"
                                           : temp_type;

                // set linkage id
                long temp_id = ++temp_counts_[eq_type];
                link_to_sub->id() = temp_id;

                scaling_map last_flop_map = flop_map_;

                /// substitute linkage in all equations

                vector<string> eq_keys = get_equation_keys();
                size_t num_subs = 0; // number of substitutions made

#pragma omp parallel for schedule(guided) default(none) firstprivate(allow_equality, link_to_sub) \
                shared(equations_, eq_keys) reduction(+:num_subs)
                for (const auto &eq_name: eq_keys) { // iterate over equations in parallel
                    // get equation
                    Equation &equation = equations_[eq_name]; // get equation
                    size_t this_subs = equation.substitute(link_to_sub, allow_equality);
                    bool madeSub = this_subs > 0;
                    if (madeSub) {
                        // sort tmps in equation
                        equation.rearrange();
                        num_subs += this_subs;
                    }
                }
                totalSubs += num_subs; // add number of substitutions to total

                // add linkage to ignore linkages
                link_to_sub->forget(); // clear linkage history
                ignore_linkages.insert(link_to_sub);
                test_linkages.erase(link_to_sub);

                // collect new scaling
                collect_scaling();

                if (num_subs == 0) {
                    temp_counts_[eq_type]--;
                    continue;
                }
                else {

                    // add linkage to equations
                    const Term &precon_term = add_tmp(link_to_sub, equations_[eq_type], 1.0);

                    // print linkage
                    {
                        cout << " ====> Substitution " << to_string(temp_id) << " <==== " << endl;
                        cout << " ====> " << precon_term << endl;
                        cout << " Difference: " << flop_map_ - last_flop_map << std::endl << endl;
                    }

                    // add linkage to this set
                    saved_linkages_[eq_type].insert(link_to_sub); // add tmp to tmps
                    found_any = true; // set found any flag to true
                }

                num_terms = 0;
                for (const auto &eq_pair: equations_) {
                    const Equation &equation = eq_pair.second;
                    num_terms += equation.size();
                }

                // print flop map
                {

                    // print total time elapsed
                    substitute_timer.stop();
                    update_timer.stop();
                    total_timer.stop();

                    cout << "---------------------------- Remaining candidates: " << test_linkages.size();
                    cout << " ----------------------------" << endl << endl;

                    cout << "                  Net time: " << total_timer.elapsed() << endl;
                    cout << "              Reorder Time: " << reorder_timer.elapsed() << endl;
                    cout << "               Update Time: " << update_timer.elapsed() << endl;
                    cout << "         Average Sub. Time: " << substitute_timer.average_time() << endl;
                    cout << "           Number of terms: " << num_terms << endl;
                    cout << "    Number of Contractions: " << flop_map_.total() << endl;
                    cout << "        Substitution count: " << num_subs << endl;
                    cout << "  Total Substitution count: " << totalSubs << endl << endl;
                }

                total_timer.start();
                update_timer.start();
                // break if not batching substitutions or if we have reached the batch size
                // at batch_size_=1 this will only substitute the best link found and then completely regenerate the results.
                // otherwise it will substitute the best batch_size_ number of linkages and then regenerate the results.
                if (!batched_ || ++batch_count >= batch_size_ || temp_counts_[eq_type] > max_temps_) {
                    substitute_timer.stop();
                    break;
                }
            }

            update_timer.stop();
        }

        update_timer.start();

        // remove all prior substituted linkages
        for (const auto &[type, linkages]: saved_linkages_) {
            ignore_linkages += linkages;
        }

        // update test linkages
        test_linkages = all_links_ - ignore_linkages;

        bool recompute = test_linkages.empty();
        bool last_empty = recompute;

        // gradually increase max depth if we have not found any linkages (start from lowest depth; only if batching)
        while (test_linkages.empty() && recompute) {

            // merge terms if allowed
            num_merged = merge_terms();
            total_num_merged += num_merged;

            size_t num_fused = merge_intermediates();
            if (num_fused > 0) {
                total_num_merged += num_fused;
                cout << "Fused " << num_fused << " terms." << endl;
            }

            prune();

            Term::max_depth_ = ++current_depth; // increase max depth

            {
                cout << "Regenerating test set with depth " << flush;
                if (current_depth >= org_max_depth)
                     cout << "(max) ... " << flush;
                else cout << "(" << current_depth << ") ... " << flush;
            }

            // regenerate all valid linkages with the new depth
            make_all_links(true);

            // update test linkages
            test_linkages = all_links_ - ignore_linkages;

            // clear linkages within ignore set and test set
            for (auto &linkage: ignore_linkages)
                linkage->forget();
            for (auto &linkage: test_linkages)
                linkage->forget();

            cout << " Done (" << "found " << test_linkages.size() << ")" << endl;

            if (current_depth >= org_max_depth)
                break; // break if we have reached the maximum depth

            if (last_empty && test_linkages.empty()) {
                current_depth = org_max_depth - 1; // none found twice, so test up to max depth

                if (!batched_) break; // break if not batching
            }
            last_empty = test_linkages.empty();
        }

        update_timer.stop();
        first_pass = false;

    // end while loop when no more substitutions can be made, or we have reached the maximum number of temps
    }

    // merge terms if allowed
    num_merged = merge_terms();
    total_num_merged += num_merged;

    // merge intermediates
    size_t num_fused = merge_intermediates();
    if (num_fused > 0) {
        total_num_merged += num_fused;
        cout << "Fused " << num_fused << " terms." << endl;
    }

    Term::max_depth_ = org_max_depth;

    // resort tmps
    for (auto & [type, eq] : equations_)
        eq.rearrange();

    substitute_timer.stop(); // stop timer for substitution
    update_timer.stop();

    // recollect scaling of equations (now including sigma vectors)
    collect_scaling(true, true);


    if (temp_counts_[temp_type] >= max_temps_)
        cout << "WARNING: Maximum number of substitutions reached. " << endl << endl;

    if (!found_any) {
        cout << "No substitutions found." << endl << endl;
        return;
    }

    // print total time elapsed
    cout << endl << "=================================> Substitution Summary <=================================" << endl;

    num_terms = 0;
    for (const auto& eq_pair : equations_) {
        const Equation &equation = eq_pair.second;
        num_terms += equation.size();
    }
    for (const auto & [type, count] : temp_counts_) {
        if (count == 0)
            continue;
        cout << "    Found " << count << " " << type << endl;
    }

    total_timer.stop();
    cout << "    Total Time: " << total_timer.elapsed() << endl;
    total_timer.start();

    cout << "    Total number of terms: " << num_terms << endl;
    cout << "    Total terms merged: " << total_num_merged << endl;
    cout << "    Total contractions: " << flop_map_.total() << (format_sigma ? " (ignoring assignments of intermediates)" : "") << endl;
    cout << endl;

    cout << " ===================================================="  << endl << endl;

    total_timer.stop();
}

PQGraph PQGraph::clone() const {
    // make initial copy
    PQGraph copy = *this;

    // copy equations and make deep copies of terms
    map<string, Equation> copy_equations;
    for (auto & [name, eq] : equations_) {
        copy_equations[name] = eq.clone();
    }
    copy.equations_ = copy_equations;

    // copy all linkages
    map<string, linkage_set> copy_saved_linkages;
    for (const auto & [type, linkages] : saved_linkages_) {
        linkage_set new_linkages;
        for (const auto & linkage : linkages) {
            ConstLinkagePtr link = as_link(linkage->clone());
            new_linkages.insert(link) ;
        }
        copy_saved_linkages[type] = new_linkages;
    }
    copy.saved_linkages_ = copy_saved_linkages;

    return copy;
}

void PQGraph::make_scalars() {

    cout << "Finding scalars..." << flush;
    // find scalars in all equations and substitute them
    linkage_set scalars = saved_linkages_["scalar"];
    for (auto &[name, eq]: equations_) {
        // do not make scalars in scalar equation
        if (name == "scalar") continue;
        eq.make_scalars(scalars, temp_counts_["scalar"]);
    }
    cout << " Done" << endl;

    // create new equation for scalars if it does not exist
    if (equations_.find("scalar") == equations_.end()) {
        equations_.emplace("scalar", Equation(make_shared<Vertex>("scalar"), {}));
        equations_["scalar"].is_temp_equation_ = true;
    }

    if (Equation::no_scalars_) {

        cout << "Removing scalars from equations..." << endl;

        // remove scalar equation
        equations_.erase("scalar");

        // remove scalars from all equations
        vector<string> to_remove;
        for (auto &[name, eq]: equations_) {
            vector<Term> new_terms;
            for (auto &term: eq.terms()) {
                bool has_scalar = false;
                for (auto &op: term.rhs()) {
                    if (op->is_linked() && op->is_scalar()) {
                        has_scalar = true;
                        break;
                    }
                }

                if (!has_scalar)
                    new_terms.push_back(term);
            }
            // if no terms left, remove equation
            if (new_terms.empty())
                to_remove.push_back(name);
            else
                eq.terms() = new_terms;
        }

        // remove equations
        for (const auto &name: to_remove) {
            cout << "Removing equation: " << name << " (no terms left after removing scalars)" << endl;
            equations_.erase(name);
        }

        // remove scalars from saved linkages
        scalars.clear();
    }

    // add new scalars to all linkages and equations
    for (const auto &scalar: scalars) {
        // add term to scalars equation
        add_tmp(scalar, equations_["scalar"]);
        saved_linkages_["scalar"].insert(scalar);

        // print scalar
        cout << scalar->str() << " = " << *scalar << endl;
    }

    // remove comments from scalars
    for (Term &term: equations_["scalar"].terms())
        term.comments() = {}; // comments should be self-explanatory

    // collect scaling
    collect_scaling(true);
    is_assembled_ = true;
}