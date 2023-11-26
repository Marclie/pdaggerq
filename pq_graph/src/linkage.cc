//
// pdaggerq - A code for bringing strings of creation / annihilation operators to normal order.
// Filename: linkage.cc
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

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>
#include <cstring>
#include "../include/linkage.h"

namespace pdaggerq {

    /********** Constructors **********/

    inline Linkage::Linkage(const VertexPtr &left, const VertexPtr &right, bool is_addition) : Vertex() {

        // set inputs
        if (!left->is_linked() && !right->is_linked()) {
            // a binary linkage is associative (left and right are interchangeable)
            // sort left and right vertices by name to prevent duplicates
            // TODO: make sure this works with Term::is_compatible()
            if (left->name() < right->name()) {
                 left_ =  left;
                right_ = right;
            } else {
                 left_ = right;
                right_ =  left;
            }
        } else {
            // a linkage with more than two vertices is not associative
            left_  =  left;
            right_ = right;
        }

        // count_ the left and right vertices
        nvert_ = 2;

        // determine the depth of the linkage
        if (left_->is_linked()){
            // subtract 1 and add the number of vertices in left
            --nvert_ += as_link(left_)->nvert_;
        }
        if (right_->is_linked()){
            // subtract 1 and add the number of vertices in right
            --nvert_ += as_link(right_)->nvert_;
        }

        is_addition_ = is_addition;

        // create hash for the name (should be unique and faster for comparisons)
        base_name_ = left_->name_ + "\t" + right_->name_;

        // build internal and external lines
        set_links();

        // set properties
        rank_  = lines_.size();
        shape_ = mem_scale_;
        has_blk_ = left_->has_blk_ || right_->has_blk_;
        update_name();

        // create mapping of indices of internal and external lines from left to right vertices
        connect_lines();

        // check if left or right vertices are also linkages and set parent linkage
//        if ( left_->is_linked())  left_parent_ = as_link( left_);
//        if (right_->is_linked()) right_parent_ = as_link(right_);

    }

    inline void Linkage::set_links() {

        // clear internal and external lines and connections
        int_lines_.clear();
        lines_.clear();

        // grab data from left and right vertices
        uint_fast8_t left_size = left_->size();
        uint_fast8_t right_size = right_->size();
        uint_fast8_t total_size = left_size + right_size;

        const auto &left_lines = left_->lines();
        const auto &right_lines = right_->lines();

        // handle scalars
        if (left_size == 0 && right_size == 0) {
            return; // both vertices are scalars (no lines)
        }
        else if (left_size == 0) { // if left is a scalar, just use right_lines as linkage
            lines_ = right_lines;
            mem_scale_ = right_->shape_;
            flop_scale_ = right_->shape_;
            return;
        } else if (right_size == 0) { // if right is a scalar, just use left_lines as linkage
            lines_  = left_lines;
            mem_scale_ = left_->shape_;
            flop_scale_ = left_->shape_;
            return;
        }

        // reserve space for internal and external lines
        lines_.reserve(total_size);


        // populate left lines
        map<Line, uint_fast8_t> line_populations;
        for (const auto &line : left_lines)
            line_populations[line]++;

        // populate right lines
        for (const auto &line : right_lines)
            line_populations[line]++;

        // use count to determine if the line is internal or external
        // lines are sorted via map insertion
        for (auto &[line, freq] : line_populations) {
            if (freq == 1) {
                // this line is external
                lines_.push_back(line);

                // update mem scale
                mem_scale_ += line;

                // check if external line is a sigma or density fitting index
                if (line.sig_) is_sigma_ = true;
                else if (line.den_) is_den_ = true;

            } else {
                // this line is internal
                int_lines_.insert(line);
            }

            // update flop scale
            flop_scale_ += line;
        }

    }

    inline void Linkage::connect_lines() {

        // clear connections
        int_connec_.clear();
        r_ext_idx_.clear();
        l_ext_idx_.clear();

        // grab data from left and right vertices
        const auto &left_lines = left_->lines();
        const auto &right_lines = right_->lines();

        // assume all lines are external
        uint_fast8_t left_size = left_lines.size();
        uint_fast8_t right_size = right_lines.size();
        for (int i = 0; i < left_size; ++i)
            l_ext_idx_.emplace(i);
        for (int i = 0; i < right_size; ++i)
            r_ext_idx_.emplace(i);


        bool search_left = left_lines < right_lines; // search the smaller set of lines
        uint_fast8_t search_size = search_left ? left_size : right_size;

        // build internal connections (note, this does not explicitly check for repeated lines)
        for (uint_fast8_t i = 0; i < search_size; i++) {
            const Line &line = search_left ? left_lines[i] : right_lines[i];

            // find line in internal lines
            auto it = int_lines_.find(line);
            if (it == int_lines_.end()) continue; // line is not internal

            // get index of line in the other vertex
            uint_fast8_t other_idx;
            if (search_left) { // if searching left, find line in right
                auto other_it = std::find(right_lines.begin(), right_lines.end(), line);
                other_idx = std::distance(right_lines.begin(), other_it);
            } else { // if searching right, find line in left
                auto other_it = std::find(left_lines.begin(), left_lines.end(), line);
                other_idx = std::distance(left_lines.begin(), other_it);
            }

            // add indices to connections and remove index from external indices
            if (search_left) {
                int_connec_.emplace(i, other_idx);
                l_ext_idx_.erase(i);
                r_ext_idx_.erase(other_idx);
            } else {
                int_connec_.emplace(other_idx, i);
                l_ext_idx_.erase(other_idx);
                r_ext_idx_.erase(i);
            }

        }
    }

    LinkagePtr Linkage::link(const vector<VertexPtr> &op_vec) {
        uint_fast8_t op_vec_size = op_vec.size();

        if (op_vec_size == 0) {
            throw invalid_argument("Linkage::link(): op_vec must have at least two elements");
        } else if (op_vec_size == 1) {
            // this is a hack to allow for the creation of a LinkagePtr from a single VertexPtr
            // TODO: find a better way to do this
            return as_link(make_shared<Vertex>("") * op_vec[0]);
        }

        VertexPtr linkage = op_vec[0] * op_vec[1];
        for (uint_fast8_t i = 2; i < op_vec_size; i++)
            linkage = std::move(linkage * op_vec[i]);

        return as_link(linkage);
    }

    vector<LinkagePtr> Linkage::links(const vector<VertexPtr> &op_vec){
        uint_fast8_t op_vec_size = op_vec.size();
        if (op_vec_size <= 1) {
            throw invalid_argument("Linkage::link(): op_vec must have at least two elements");
        }

        vector<LinkagePtr> linkages(op_vec_size - 1);

        VertexPtr linkage = op_vec[0] * op_vec[1];
        linkages[0] = as_link(linkage);
        for (uint_fast8_t i = 2; i < op_vec_size; i++) {
            linkage = linkage * op_vec[i];
            linkages[i - 1] = as_link(linkage);
        }

        return linkages;
    }

    pair<vector<shape>,vector<shape>> Linkage::scale_list(const vector<VertexPtr> &op_vec) {

        uint_fast8_t op_vec_size = op_vec.size();
        if (op_vec_size <= 1) {
            throw invalid_argument("link(): op_vec must have at least two elements");
        }

        vector<shape> flop_list;
        vector<shape> mem_list;

        LinkagePtr linkage = as_link(op_vec[0] * op_vec[1]);
        flop_list.push_back(linkage->flop_scale_);
        mem_list.push_back(linkage->mem_scale_);

        for (uint_fast8_t i = 2; i < op_vec_size; i++) {
            linkage = as_link(linkage * op_vec[i]);
            flop_list.push_back(linkage->flop_scale_);
            mem_list.push_back(linkage->mem_scale_);
        }

        return {flop_list, mem_list};
    }

    tuple<LinkagePtr, vector<shape>, vector<shape>> Linkage::link_and_scale(const vector<VertexPtr> &op_vec) {
        uint_fast8_t op_vec_size = op_vec.size();
        if (op_vec_size <= 1) {
            throw invalid_argument("link(): op_vec must have at least two elements");
        }

        vector<shape> flop_list;
        vector<shape> mem_list;

        LinkagePtr linkage = as_link(op_vec[0] * op_vec[1]);
        flop_list.push_back(linkage->flop_scale_);
        mem_list.push_back(linkage->mem_scale_);

        for (uint_fast8_t i = 2; i < op_vec_size; i++) {
            linkage = as_link(linkage * op_vec[i]);
            flop_list.push_back(linkage->flop_scale_);
            mem_list.push_back(linkage->mem_scale_);
        }

        return {linkage, flop_list, mem_list};
    }

    Linkage::Linkage() {
        id_ = -1;
        flop_scale_ = shape();
        mem_scale_ = shape();
    }

    /****** operator overloads ******/

    bool Linkage::operator==(const Linkage &other) const {

        // check if both linkage are empty or not
        if (empty()) return other.empty();

        // check if linkage type is the same
        if (is_addition_ != other.is_addition_) return false;

        // check the depth of the linkage
        if (nvert_ != other.nvert_) return false;

        // check if left and right vertices are linked in the same way
        if ( left_->is_linked() ^  other.left_->is_linked()) return false;
        if (right_->is_linked() ^ other.right_->is_linked()) return false;

        // recursively check if left linkages are equivalent
        if (left_->is_linked()) {
            if (*as_link(left_) != *as_link(other.left_)) return false;
        }

        // check if right linkages are equivalent
        if (right_->is_linked()) {
            if (*as_link(right_) != *as_link(other.right_)) return false;
        }

        // check that scales are equal
        if (flop_scale_ != other.flop_scale_) return false;
        if (mem_scale_  !=  other.mem_scale_) return false;

        // check linkage maps
        if (l_ext_idx_  !=  other.l_ext_idx_) return false;
        if (r_ext_idx_  !=  other.r_ext_idx_) return false;
        if (int_connec_ != other.int_connec_) return false;

        // check if linkage vertices (and external lines) are equivalent
        if (!equivalent(other)) return false;

        // check if left and right vertices are equivalent
        if ( !left_->equivalent( *other.left_)) return false;
        if (!right_->equivalent(*other.right_)) return false;


        // if all tests pass, return true
        return true;
    }

    // repeat code from == operator, but invert the logic to end recursion early if possible
    bool Linkage::operator!=(const Linkage &other) const {
        return !(*this == other);
    }

    pair<bool, bool> Linkage::permuted_equals(const Linkage &other) const {
        // first test if the linkages are equal
        if (*this == other) return {true, false};

        // test if the linkages have the same number of vertices
        if (nvert_ != other.nvert_) return {false, false};

        // extract total vector of vertices
        const vector<VertexPtr> &this_vert = to_vector();
        const vector<VertexPtr> &other_vert = other.to_vector();

        // check if the vertices are isomorphic and keep track of the number of permutations
        bool swap_sign = false;
        for (size_t i = 0; i < nvert_; i++) {
            bool odd_perm = false;
            bool same_to_perm = is_isomorphic(*this_vert[i], *other_vert[i], odd_perm);
            if (!same_to_perm) return {false, false};
            if (odd_perm) swap_sign = !swap_sign;
        }

        // if the linkages are isomorphic, return true and if the permutation is odd
        return {true, swap_sign};

    }


    string Linkage::str(bool make_generic, bool include_lines) const {

        if (!is_temp()) { // TODO: this might be annoying if we want to reuse a tmp. We will see when we get there...
            // this is not an intermediate vertex (generic linkage).
            // return the str of the left and right vertices
            return tot_str(false, true);
        }

        if (!make_generic) return str();

        // prepare output string as a map of tmps (or reuse_tmps) to a generic name
        string generic_str = is_reused_ ? "reuse_tmps_[\"" : "tmps_[\"";

        // add dimension string
        generic_str += dimstring();
        generic_str += "_";

        // format for scalars
        if (is_scalar())
            generic_str = "scalars_[\"";


        // use id_ to create a generic name
        if (id_ >= 0)
            generic_str += to_string(id_);

        generic_str += "\"]";

        if (include_lines) // if lines are included, add them to the generic name (default)
            generic_str += line_str();

        // create a generic vertex that has the same lines as this linkage.
        // this adds the spin and ov strings to name
        // return its string representation
        return generic_str;
    }

    string Linkage::tot_str(bool expand, bool make_dot) const {

        if (empty()) return "";

        // do not expand linkages that are not intermediates
        if (!is_temp()) expand = false;

        // prepare output string
        string output, left_string, right_string;

        // build left string representation recursively
        if (left_->is_linked() && expand) left_string = as_link(left_)->tot_str(expand, make_dot);
        else left_string = left_->str();

        // build right string representation recursively
        if (right_->is_linked() && expand) right_string = as_link(right_)->tot_str(expand, make_dot);
        else right_string = right_->str();
        
        
        if (!is_addition_) output = left_string + " * " + right_string;
        else { output = "(" + left_string + " + " + right_string + ")"; }


        // if rank == 0, all lines are internal; requires dot() function call
        if (rank() == 0 && !is_addition_ && make_dot) {
            // add 'dot(' after '='
            output = "dot(" + output;

            // find the last star in output
            size_t last_star = output.rfind(" * ");

            // find last ' * '; replace with ', '
            output.replace(last_star, 3, ", ");

            // add closing parenthesis
            output += ")";
        }

        return output;
    }

    inline void Linkage::to_vector(vector<VertexPtr> &result, size_t &i, bool full_expand) const {

        if (empty()) return;

        // get left vertex
        if (left_->is_linked()) {
            const LinkagePtr left_linkage = as_link(left_);

            // check if left linkage is a tmp
            if (!full_expand && left_linkage->is_temp()) {
                // if this is a tmp and we are not expanding, add it to the result and return
                result[i++] = left_linkage;
            } else {

                // compute the vertices recursively and save them
                left_linkage->to_vector();

                // add left vertices to result
                for (const auto &vertex: left_linkage->all_vert_)
                    result[i++] = vertex;
            }

        } else result[i++] = left_;

        // get right vertex
        if (right_->is_linked()) {
            const LinkagePtr right_linkage = as_link(right_);

            // check if right linkage is a tmp
            if (!full_expand && right_linkage->is_temp()) {
                // if this is a tmp and we are not expanding, add it to the result and return
                result[i++] = right_linkage;
            } else {

                // compute the vertices recursively and save them
                right_linkage->to_vector();

                for (const auto &vertex: right_linkage->all_vert_)
                    result[i++] = vertex;
            }

        } else result[i++] = right_;
    }

    const vector<VertexPtr> &Linkage::to_vector(bool regenerate, bool full_expand) const {

        std::lock_guard<std::mutex> lock(mtx_);  // Lock the mutex for the scope of the function
        if (all_vert_.empty() || regenerate){ // the vertices are not known
            // compute the vertices recursively
            all_vert_ = vector<VertexPtr>(nvert_); // store the vertices in all_vert_ for next query
            size_t i = 0;
            to_vector(all_vert_, i, full_expand);
            if (i != nvert_)
                all_vert_.resize(i);
            return all_vert_;
        }

        // the vertices are known
        return all_vert_;
    }

    void Linkage::clone_link(const Linkage &other) {
        // Lock the mutex for the scope of the function
        std::lock_guard<std::mutex> lock(mtx_);

        // call base class copy constructor
        this->Vertex::operator=(other);

        // fill linkage data (shallow copy, but should not be modified either way) TODO: enforce this
        left_ = other.left_;
        right_ =other.right_;

//        left_parent_ = other.left_parent_;
//        right_parent_ = other.right_parent_;

        id_ = other.id_;
        nvert_ = other.nvert_;

        int_connec_ = other.int_connec_;
        l_ext_idx_ = other.l_ext_idx_;
        r_ext_idx_ = other.r_ext_idx_;

        int_lines_ = other.int_lines_;

        flop_scale_ = other.flop_scale_;
        mem_scale_ = other.mem_scale_;

        is_addition_ = other.is_addition_;
        is_reused_ = other.is_reused_;

        all_vert_ = other.all_vert_;
        for (auto &vertex : all_vert_)
            vertex = copy_vert(vertex);
    }

    Linkage::Linkage(const Linkage &other) {
        clone_link(other);
    }

    Linkage &Linkage::operator=(const Linkage &other) {
        // check for self-assignment
        if (this == &other) return *this;
        else clone_link(other);

        return *this;
    }

    void Linkage::move_link(Linkage &&other) {
        // Lock the mutex for the scope of the function
        std::lock_guard<std::mutex> lock(mtx_);

        // call base class move constructor
        this->Vertex::operator=(other);

        // move linkage data
        left_ = std::move(other.left_);
        right_ = std::move(other.right_);

//        left_parent_ = std::move(other.left_parent_);
//        right_parent_ = std::move(other.right_parent_);

        id_ = other.id_;
        nvert_ = other.nvert_;

        int_connec_ = std::move(other.int_connec_);
        l_ext_idx_ = std::move(other.l_ext_idx_);
        r_ext_idx_ = std::move(other.r_ext_idx_);

        int_lines_ = std::move(other.int_lines_);

        flop_scale_ = std::move(other.flop_scale_);
        mem_scale_ = std::move(other.mem_scale_);

        is_addition_ = other.is_addition_;
        is_reused_ = other.is_reused_;

        all_vert_ = std::move(other.all_vert_);
    }

    Linkage::Linkage(Linkage &&other) noexcept {

        // call move constructor
        move_link(std::move(other));
    }

    Linkage &Linkage::operator=(Linkage &&other) noexcept {
        // check for self-assignment
        if (this == &other) return *this;
        else move_link(std::move(other));

        return *this;
    }

    extern VertexPtr operator*(const VertexPtr &left, const VertexPtr &right){
        return make_shared<Linkage>(left, right, false);
    }

    extern VertexPtr operator+(const VertexPtr &left, const VertexPtr &right){
        return make_shared<Linkage>(left, right, false);
    }

    extern VertexPtr copy_vert(const VertexPtr &vertex){
        if ( vertex->is_linked() )
             return make_shared<Linkage>(*as_link(vertex));
        else return make_shared<Vertex>(*vertex);

    }

} // pdaggerq
