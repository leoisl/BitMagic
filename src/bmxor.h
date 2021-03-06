#ifndef BMXORFUNC__H__INCLUDED__
#define BMXORFUNC__H__INCLUDED__
/*
Copyright(c) 2002-2019 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/

/*! \file bmxor.h
    \brief Functions and utilities for XOR filters (internal)
*/

#include "bmdef.h"
#include "bmutil.h"


namespace bm
{

/**
    XOR complementarity type between 2 blocks
    @internal
 */
enum xor_complement_match
{
    e_no_xor_match = 0,
    e_xor_match_GC,
    e_xor_match_BC,
    e_xor_match_iBC,
    e_xor_match_EQ
};

/*!
    Function calculates basic complexity statistics on XOR product of
    two blocks (b1 XOR b2)
    @ingroup bitfunc
    @internal
*/
inline
void bit_block_xor_change32(const bm::word_t* BMRESTRICT block,
                            const bm::word_t* BMRESTRICT xor_block,
                            unsigned size,
                            unsigned* BMRESTRICT gc,
                            unsigned* BMRESTRICT bc) BMNOEXCEPT
{
    BM_ASSERT(gc && bc);

    unsigned gap_count = 1;
    unsigned bit_count = 0;

    bm::word_t  w, w0, w_prev, w_l;
    w = w0 = *block ^ *xor_block;
    bit_count += word_bitcount(w);

    const int w_shift = int(sizeof(w) * 8 - 1);
    w ^= (w >> 1);
    gap_count += bm::word_bitcount(w);
    gap_count -= (w_prev = (w0 >> w_shift)); // negative value correction

    const bm::word_t* block_end = block + size; // bm::set_block_size;
    for (++block, ++xor_block; block < block_end; ++block, ++xor_block)
    {
        w = w0 = *block ^ *xor_block;
        bit_count += bm::word_bitcount(w);
        ++gap_count;
        if (!w)
        {
            gap_count -= !w_prev;
            w_prev = 0;
        }
        else
        {
            w ^= (w >> 1);
            gap_count += bm::word_bitcount(w);

            w_l = w0 & 1;
            gap_count -= (w0 >> w_shift);  // negative value correction
            gap_count -= !(w_prev ^ w_l);  // word border correction

            w_prev = (w0 >> w_shift);
        }
    } // for

    *gc = gap_count;
    *bc = bit_count;
}

/*!
    Function calculates number of times when bit value changed
    @internal
*/
inline
void bit_block_xor_change(const bm::word_t* BMRESTRICT block,
                              const bm::word_t* BMRESTRICT xor_block,
                              unsigned size,
                              unsigned* BMRESTRICT gc,
                              unsigned* BMRESTRICT bc) BMNOEXCEPT
{
#ifdef VECT_BLOCK_XOR_CHANGE
    VECT_BLOCK_XOR_CHANGE(block, xor_block, size, gc, bc);
#else
    bm::bit_block_xor_change32(block, xor_block, size, gc, bc);
#endif
}

/**
    Structure to compute XOR gap-count profile by sub-block waves
    @ingroup bitfunc
    @internal
*/
struct block_waves_xor_descr
{
    // measurements of the original block
    unsigned short sb_gc[bm::block_waves]; ///< GAP counts
    unsigned short sb_bc[bm::block_waves]; ///< BIT counts

    // measurements of block XOR mask
    unsigned short sb_xor_gc[bm::block_waves]; ///< XOR-mask GAP count
    unsigned short sb_xor_bc[bm::block_waves]; ///< XOR-mask GAP count
};

/**
    Compute reference (non-XOR) 64-dim complexity descriptor for the
    target block. Phase 1 of the XOR filtering process is to establish
    the base metric

    @internal
*/
inline
void compute_complexity_descr(
                        const bm::word_t* BMRESTRICT block,
                        block_waves_xor_descr& BMRESTRICT x_descr) BMNOEXCEPT
{
    // TODO: SIMD (for loop can go inside VECT to minimize LUT re-inits)
    for (unsigned i = 0; i < bm::block_waves; ++i)
    {
        unsigned off = (i * bm::set_block_digest_wave_size);
        const bm::word_t* sub_block = block + off;
        unsigned gc, bc;
        // TODO: optimize to compute GC and BC in a single pass
        #if defined(VECT_BLOCK_CHANGE)
            gc = VECT_BLOCK_CHANGE(sub_block, bm::set_block_digest_wave_size);
        #else
            gc = bm::bit_block_change32(sub_block, bm::set_block_digest_wave_size);
        #endif
        x_descr.sb_gc[i] = (unsigned short) gc;
        bc = bm::bit_count_min_unroll(
                    sub_block, sub_block + bm::set_block_digest_wave_size);
        x_descr.sb_bc[i] = (unsigned short) bc;
    } // for i
}


/**
    Compute reference complexity descriptor based on XOR vector.
    Returns the digest of sub-blocks where XOR filtering improved the metric
    (function needs reference to estimate the improvement).

    part of Phase 2 of the XOR filtering process

    @sa compute_sub_block_complexity_descr

    @internal
*/
inline
void compute_xor_complexity_descr(
                        const bm::word_t* BMRESTRICT block,
                        const bm::word_t* BMRESTRICT xor_block,
                        bm::block_waves_xor_descr& BMRESTRICT x_descr,
                        bm::xor_complement_match& BMRESTRICT match_type,
                        bm::id64_t& BMRESTRICT digest,
                        unsigned& BMRESTRICT block_gain) BMNOEXCEPT
{
    // digest of all ZERO sub-blocks
    bm::id64_t d0 = ~bm::calc_block_digest0(block);

    // Pass 1: compute XOR descriptors
    //
    for (unsigned i = 0; i < bm::block_waves; ++i)
    {
        unsigned off = (i * bm::set_block_digest_wave_size);
        const bm::word_t* sub_block = block + off;
        const bm::word_t* xor_sub_block = xor_block + off;

        unsigned xor_gc, xor_bc;
        bm::bit_block_xor_change(sub_block, xor_sub_block,
                                 bm::set_block_digest_wave_size,
                                 &xor_gc, &xor_bc);
        x_descr.sb_xor_gc[i] = (unsigned short)xor_gc;
        x_descr.sb_xor_bc[i] = (unsigned short)xor_bc;
    } // for i

    // Pass 2: find the best match
    //
    unsigned block_gc_gain(0), block_bc_gain(0), block_ibc_gain(0);
    bm::id64_t gc_digest(0), bc_digest(0), ibc_digest(0);
    const unsigned wave_max_bits = bm::set_block_digest_wave_size * 32;

    for (unsigned i = 0; i < bm::block_waves; ++i)
    {
        bm::id64_t dmask = (1ull << i);
        if (d0 & dmask)
            continue;

        unsigned xor_gc = x_descr.sb_xor_gc[i];
        if (xor_gc <= 1)
        {
            gc_digest |= dmask;
            block_gc_gain += x_descr.sb_gc[i];
        }
        else if (xor_gc < x_descr.sb_gc[i]) // detected improvement in GAPs
        {
            gc_digest |= dmask;
            block_gc_gain += (x_descr.sb_gc[i] - xor_gc);
        }
        unsigned xor_bc = x_descr.sb_xor_bc[i];
        if (xor_bc < x_descr.sb_bc[i]) // improvement in BITS
        {
            bc_digest |= dmask;
            block_bc_gain += (x_descr.sb_bc[i] - xor_bc);
        }
        unsigned xor_ibc = wave_max_bits - xor_bc;
        unsigned wave_ibc = wave_max_bits - x_descr.sb_bc[i];
        if (xor_ibc < wave_ibc) // improvement in 0 BITS
        {
            ibc_digest |= dmask;
            block_ibc_gain += (wave_ibc - xor_ibc);
        }

    } // for i

    // Find the winning metric and its digest mask
    //

    if (!(block_gc_gain | block_bc_gain | block_ibc_gain)) // match not found
    {
        bm::id64_t d0_x = ~bm::calc_block_digest0(xor_block);
        if (d0 == d0_x) // TODO: why are we doing this stunt?!
        {
            match_type = bm::e_xor_match_GC;
            block_gain = bm::block_waves;
            digest = d0;
            return;
        }

        match_type = bm::e_no_xor_match; block_gain = 0; digest = 0;
        return;
    }

    if (block_gc_gain > block_bc_gain)
    {
        if (block_gc_gain > block_ibc_gain)
        {
            match_type = bm::e_xor_match_GC;
            block_gain = block_gc_gain;
            digest = gc_digest;
            return;
        }
    }
    else // GC_gain <= BC_gain
    {
        if (block_bc_gain > block_ibc_gain)
        {
            match_type = bm::e_xor_match_BC;
            block_gain = block_bc_gain;
            digest = bc_digest;
            return;
        }
    }
    match_type = bm::e_xor_match_iBC;
    block_gain = block_ibc_gain;
    digest = ibc_digest;
    return;
}

/**
    Build partial XOR product of 2 bit-blocks using digest mask

    @param target_block - target := block ^ xor_block
    @param block - arg1
    @param xor_block - arg2
    @param digest - mask for each block wave to XOR (1) or just copy (0)

    @internal
*/
inline
void bit_block_xor(bm::word_t*  target_block,
                   const bm::word_t*  block, const bm::word_t*  xor_block,
                   bm::id64_t digest) BMNOEXCEPT
{
    BM_ASSERT(target_block);
    BM_ASSERT(block);
    BM_ASSERT(xor_block);
    BM_ASSERT(digest);

#ifdef VECT_BIT_BLOCK_XOR
    VECT_BIT_BLOCK_XOR(target_block, block, xor_block, digest);
#else
    for (unsigned i = 0; i < bm::block_waves; ++i)
    {
        const bm::id64_t mask = (1ull << i);
        unsigned off = (i * bm::set_block_digest_wave_size);
        const bm::word_t* sub_block = block + off;
        bm::word_t* t_sub_block = target_block + off;

        const bm::word_t* sub_block_end = sub_block + bm::set_block_digest_wave_size;

        if (digest & mask) // XOR filtered sub-block
        {
            const bm::word_t* xor_sub_block = xor_block + off;
            for (; sub_block < sub_block_end; )
                *t_sub_block++ = *sub_block++ ^ *xor_sub_block++;
        }
        else // just copy source
        {
            for (; sub_block < sub_block_end;)
                *t_sub_block++ = *sub_block++;
        }
    } // for i
#endif
}


/**
    List of reference bit-vectors with their true index associations

    Each referece vector would have two alternative indexes:
     - index(position) in the reference list
     - index(row) in the external bit-matrix (plane index)

    @internal
*/
template<typename BV>
class bv_ref_vector
{
public:
    typedef BV                                          bvector_type;
    typedef typename bvector_type::size_type            size_type;
    typedef bvector_type*                               bvector_type_ptr;
    typedef const bvector_type*                         bvector_type_const_ptr;
    typedef typename bvector_type::allocator_type       bv_allocator_type;
public:

    /// reset the collection (resize(0))
    void reset()
    {
        rows_acc_ = 0;
        ref_bvects_.resize(0); ref_bvects_rows_.resize(0);
    }

    /**
        Add reference vector
        @param bv - bvector pointer
        @param ref_idx - reference (row) index
    */
    void add(const bvector_type* bv, size_type ref_idx)
    {
        BM_ASSERT(bv);
        ref_bvects_.push_back(bv);
        ref_bvects_rows_.push_back(ref_idx);
    }

    /// Get reference list size
    size_type size() const BMNOEXCEPT { return (size_type)ref_bvects_.size(); }

    /// Get reference vector by the index in this ref-vector
    const bvector_type* get_bv(size_type idx) const BMNOEXCEPT
                                        { return ref_bvects_[idx]; }

    /// Get reference row index by the index in this ref-vector
    size_type get_row_idx(size_type idx) const BMNOEXCEPT
                        { return (size_type)ref_bvects_rows_[idx]; }

    /// not-found value for find methods
    static
    size_type not_found() BMNOEXCEPT { return ~(size_type(0)); }

    /// Find vector index by the reference index
    /// @return ~0 if not found
    size_type find(std::size_t ref_idx) const BMNOEXCEPT
    {
        size_type sz = size();
        for (size_type i = 0; i < sz; ++i) // TODO: optimization
            if (ref_idx == ref_bvects_rows_[i])
                return i;
        return not_found();
    }

    /// Find vector index by the pointer
    /// @return ~0 if not found
    size_type find_bv(const bvector_type* bv) const BMNOEXCEPT
    {
        size_type sz = size();
        for (size_type i = 0; i < sz; ++i)
            if (bv == ref_bvects_[i])
                return i;
        return not_found();
    }

    /// Reset and build vector of references from a basic bit-matrix
    ///  all NULL rows are skipped, not added to the ref.vector
    /// @sa add_vectors
    ///
    template<class BMATR>
    void build(const BMATR& bmatr)
    {
        reset();
        add_vectors(bmatr);
    }

    /// Append basic bit-matrix to the list of reference vectors
    /// @sa build
    /// @sa add_sparse_vector
    template<typename BMATR>
    void add_vectors(const BMATR& bmatr)
    {
        size_type rows = bmatr.rows();
        for (size_type r = 0; r < rows; ++r)
        {
            bvector_type_const_ptr bv = bmatr.get_row(r);
            if (bv)
                add(bv, rows_acc_ + r);
        } // for r
        rows_acc_ += unsigned(rows);
    }

    /// Add bit-transposed sparse vector as a bit-matrix
    /// @sa add_vectors
    ///
    template<class SV>
    void add_sparse_vector(const SV& sv)
    {
        add_vectors(sv.get_bmatrix());
    }


protected:
    typedef bm::heap_vector<bvector_type_const_ptr, bv_allocator_type, true> bvptr_vector_type;
    typedef bm::heap_vector<std::size_t, bv_allocator_type, true> bv_plane_vector_type;

protected:
    unsigned                 rows_acc_ = 0;     ///< total rows accumulator
    bvptr_vector_type        ref_bvects_;       ///< reference vector pointers
    bv_plane_vector_type     ref_bvects_rows_;  ///< reference vector row idxs
};

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

/**
    XOR scanner to search for complement-similarities in
    collections of bit-vectors

    @internal
*/
template<typename BV>
class xor_scanner
{
public:
    typedef bm::bv_ref_vector<BV>                bv_ref_vector_type;
    typedef BV                                   bvector_type;
    typedef typename bvector_type::size_type     size_type;

public:
    void set_ref_vector(const bv_ref_vector_type* ref_vect) BMNOEXCEPT
    { ref_vect_ = ref_vect; }

    const bv_ref_vector_type& get_ref_vector() const BMNOEXCEPT
    { return *ref_vect_; }

    /** Compute statistics for the anchor search vector
        @param block - bit-block target
    */
    void compute_x_block_stats(const bm::word_t* block) BMNOEXCEPT;

    /** Scan for all candidate bit-blocks to find mask or match
        @return true if XOR complement or matching vector found
    */
    bool search_best_xor_mask(const bm::word_t* block,
                              size_type ridx_from,
                              size_type ridx_to,
                              unsigned i, unsigned j,
                              bm::word_t* tb);

    /** Scan all candidate gap-blocks to find best XOR match
    */
    bool search_best_xor_gap(bm::gap_word_t*   tmp_buf,
                             const bm::word_t* block,
                             size_type         ridx_from,
                             size_type         ridx_to,
                             unsigned i, unsigned j);

    /**
        Validate serialization target
    */
    xor_complement_match validate_found(bm::word_t* xor_block,
                        const bm::word_t* block) const BMNOEXCEPT;

    size_type found_ridx() const BMNOEXCEPT { return found_ridx_; }
    const bm::word_t* get_found_block() const BMNOEXCEPT
    { return found_block_xor_; }
    unsigned get_x_best_metric() const BMNOEXCEPT { return x_best_metric_; }
    bm::id64_t get_xor_digest() const BMNOEXCEPT { return x_d64_; }

    /// true if completely identical block found
    /*
    bool is_eq_found() const BMNOEXCEPT
        { return !x_best_metric_
          && (x_block_mtype_ == e_xor_match_BC ||
              x_block_mtype_ == e_xor_match_GC);
        }
    */

    unsigned get_x_bc() const BMNOEXCEPT { return x_bc_; }
    unsigned get_x_gc() const BMNOEXCEPT { return x_gc_; }
    unsigned get_x_block_best() const BMNOEXCEPT
                    { return x_block_best_metric_; }


    bm::block_waves_xor_descr& get_descr() BMNOEXCEPT { return x_descr_; }

    static
    bm::xor_complement_match best_metric(unsigned bc, unsigned gc,
                                        unsigned* best_metric);
protected:

    /// Return block from the reference vector [vect_idx, block_i, block_j]
    ///
    const bm::word_t* get_ref_block(size_type ri,
                                    unsigned i, unsigned j) const BMNOEXCEPT
    {
        const bvector_type* bv = ref_vect_->get_bv(ri);
        BM_ASSERT(bv);
        const typename bvector_type::blocks_manager_type& bman =
                                                bv->get_blocks_manager();
        return bman.get_block_ptr(i, j);
    }

private:
    const bv_ref_vector_type*        ref_vect_ = 0; ///< ref.vect for XOR filter

    // target x-block statistics
    //
    bm::block_waves_xor_descr     x_descr_;  ///< XOR desriptor
    unsigned                      x_bc_;     ///< bitcount
    unsigned                      x_gc_;     ///< gap count
    unsigned                      x_best_metric_; /// dynamic min(gc, bc)

    unsigned                      x_block_best_metric_; ///< block metric
    bm::xor_complement_match      x_block_mtype_;       ///< metric type

    // scan related metrics
    bm::id64_t                    x_d64_;        ///< search digest
    size_type                     found_ridx_;   ///< match vector (in references)
    const bm::word_t*             found_block_xor_;
};

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------

template<typename BV>
void xor_scanner<BV>::compute_x_block_stats(const bm::word_t* block) BMNOEXCEPT
{
    BM_ASSERT(IS_VALID_ADDR(block));
    BM_ASSERT(!BM_IS_GAP(block));
    BM_ASSERT(ref_vect_->size() > 0);

    bm::compute_complexity_descr(block, x_descr_);
    bm::bit_block_change_bc(block, &x_gc_, &x_bc_);

    x_block_mtype_ = this->best_metric(x_bc_, x_gc_, &x_block_best_metric_);
    x_best_metric_ = x_block_best_metric_;
}

// --------------------------------------------------------------------------

template<typename BV>
bool xor_scanner<BV>::search_best_xor_mask(const bm::word_t* block,
                                           size_type ridx_from,
                                           size_type ridx_to,
                                           unsigned i, unsigned j,
                                           bm::word_t* tb)
{
    BM_ASSERT(ridx_from <= ridx_to);
    BM_ASSERT(IS_VALID_ADDR(block));
    BM_ASSERT(!BM_IS_GAP(block));
    BM_ASSERT(tb);

    if (ridx_to > ref_vect_->size())
        ridx_to = ref_vect_->size();

    bool kb_found = false;
    bm::id64_t d64 = 0;
    found_block_xor_ = 0;

    unsigned best_block_gain = 0;
    int best_ri = -1;

    for (size_type ri = ridx_from; ri < ridx_to; ++ri)
    {
        const bm::word_t* block_xor = get_ref_block(ri, i, j);
        if (!IS_VALID_ADDR(block_xor) || BM_IS_GAP(block_xor))
            continue;

        BM_ASSERT(block != block_xor);

        unsigned block_gain = 0;

        bm::id64_t xor_d64;
        bm::xor_complement_match match_type;
        bm::compute_xor_complexity_descr(block, block_xor,
                                x_descr_, match_type, xor_d64, block_gain);
        if (xor_d64) // candidate XOR block
        {
            BM_ASSERT(match_type);
            if (block_gain > best_block_gain)
            {
                best_block_gain = block_gain;
                best_ri = int(ri);
                d64 = xor_d64;
                if (block_gain >= bm::gap_max_bits)
                    break;
            }
        }
    } // for ri

    if (best_ri != -1) // found some gain, validate it now
    {
        // assumed that if XOR compression c_level is at the highest
        const float bie_bits_per_int = 3.0f; // c_level_ < 6 ? 3.75f : 3.0f;
        const unsigned bie_limit =
                        unsigned(float(bm::gap_max_bits) / bie_bits_per_int);

        unsigned xor_bc, xor_gc, xor_ibc;
        const bm::word_t* block_xor = get_ref_block(size_type(best_ri), i, j);

        bm::bit_block_xor(tb, block, block_xor, d64);
        bm::bit_block_change_bc(tb, &xor_gc, &xor_bc);

        if (!xor_bc) // completely identical block?
        {
            x_best_metric_ = xor_bc;
            kb_found = true;
            found_ridx_ = size_type(best_ri);
            found_block_xor_ = block_xor;
            x_block_mtype_ = e_xor_match_BC;
            /*
            unsigned dcnt = bm::word_bitcount64(d64);
            if (dcnt != 64)
                ++x_best_metric_;
            else
            {
                unsigned pos; (void)pos;
                BM_ASSERT(!bm::bit_find_first_diff(block, block_xor, &pos));
            }
            */
        }
        else
        {
            if (xor_gc < x_best_metric_ && xor_gc < bie_limit)
            {
                x_best_metric_ = xor_gc;
                kb_found = true;
                found_ridx_ = size_type(best_ri);
                found_block_xor_ = block_xor;
            }
            if (xor_bc < x_best_metric_ && xor_bc < bie_limit)
            {
                x_best_metric_ = xor_bc;
                kb_found = true;
                found_ridx_ = size_type(best_ri);
                found_block_xor_ = block_xor;
            }
            xor_ibc = bm::gap_max_bits - xor_bc;
            if (xor_ibc < x_best_metric_ && xor_ibc < bie_limit)
            {
                x_best_metric_ = xor_ibc;
                kb_found = true;
                found_ridx_ = size_type(best_ri);
                found_block_xor_ = block_xor;
            }
        }

    }

    x_d64_ = d64;
    return kb_found;
}

// --------------------------------------------------------------------------

template<typename BV>
bool xor_scanner<BV>::search_best_xor_gap(bm::gap_word_t*   tmp_buf,
                                          const bm::word_t* block,
                                          size_type ridx_from,
                                          size_type ridx_to,
                                          unsigned i, unsigned j)
{
    BM_ASSERT(ridx_from <= ridx_to);
    BM_ASSERT(BM_IS_GAP(block));

    if (ridx_to > ref_vect_->size())
        ridx_to = ref_vect_->size();

    const bm::gap_word_t* gap_block = BMGAP_PTR(block);
    unsigned gap_len = bm::gap_length(gap_block);
    if (gap_len <= 3)
        return false;
    unsigned bc = bm::gap_bit_count_unr(gap_block);

    bool kb_found = false;
    unsigned best_gap_metric = gap_len;
    if (bc < best_gap_metric)
        best_gap_metric = bc;

    for (size_type ri = ridx_from; ri < ridx_to; ++ri)
    {
        const bvector_type* bv = ref_vect_->get_bv(ri);
        BM_ASSERT(bv);
        const typename bvector_type::blocks_manager_type& bman = bv->get_blocks_manager();
        const bm::word_t* block_xor = bman.get_block_ptr(i, j);
        if (!IS_VALID_ADDR(block_xor) || !BM_IS_GAP(block_xor))
            continue;

        const bm::gap_word_t* gap_xor_block = BMGAP_PTR(block_xor);
        unsigned gap_xor_len = bm::gap_length(gap_block);
        if (gap_xor_len <= 3)
            continue;

        BM_ASSERT(block != block_xor);

        unsigned res_len;
        bm::gap_operation_xor(gap_block, gap_xor_block, tmp_buf, res_len);
        unsigned glen = bm::gap_length(tmp_buf);
        if (res_len > glen) // size overflow
            continue;
        unsigned res_bc = bm::gap_bit_count_unr(tmp_buf);
        if (!res_bc) // identical block
        {
            best_gap_metric = res_bc;
            kb_found = true;
            found_ridx_ = ri;
            found_block_xor_ = (const bm::word_t*)gap_xor_block;
            x_block_mtype_ = e_xor_match_BC;
        }

/*
        unsigned res_len;
        bool f = bm::gap_operation_dry_xor(gap_block, gap_xor_block, res_len, best_gap_len); */
        if ((res_len < best_gap_metric))
        {
            unsigned gain = best_gap_metric - res_len;
            if (gain > 2)
            {
                best_gap_metric = res_len;
                kb_found = true;
                found_ridx_ = ri;
                found_block_xor_ = (const bm::word_t*)gap_xor_block;
                x_block_mtype_ = e_xor_match_GC;
            }
        }
        if (res_bc < best_gap_metric)
        {
            unsigned gain = best_gap_metric - res_bc;
            if (gain > 2)
            {
                best_gap_metric = res_bc;
                kb_found = true;
                found_ridx_ = ri;
                found_block_xor_ = (const bm::word_t*)gap_xor_block;
                x_block_mtype_ = e_xor_match_BC;
            }
        }
        unsigned res_ibc = bm::gap_max_bits - res_bc;
        if (res_ibc < best_gap_metric)
        {
            unsigned gain = best_gap_metric - res_ibc;
            if (gain > 2)
            {
                best_gap_metric = res_ibc;
                kb_found = true;
                found_ridx_ = ri;
                found_block_xor_ = (const bm::word_t*)gap_xor_block;
                x_block_mtype_ = e_xor_match_iBC;
            }
        }

        if (best_gap_metric <= 1)
            break;
    } // for ri

    return kb_found;
}

// --------------------------------------------------------------------------

template<typename BV>
bm::xor_complement_match
xor_scanner<BV>::validate_found(bm::word_t* xor_block,
                                const bm::word_t* block) const BMNOEXCEPT
{
    bm::id64_t d64 = get_xor_digest();
    BM_ASSERT(d64);
    const bm::word_t* key_block = get_found_block();
    bm::bit_block_xor(xor_block, block, key_block, d64); // TODO: use one pass operation XOR + BC/GC

    unsigned bc, gc;
    bm::bit_block_change_bc(xor_block, &gc, &bc);

    unsigned xor_best_metric;
    bm::xor_complement_match mtype = best_metric(bc, gc, &xor_best_metric);

    if (mtype == e_xor_match_BC && !bc)
    {
        unsigned block_pos;
        bool found = bm::block_find_first_diff(block, key_block, &block_pos);
        if (!found)
            return e_xor_match_EQ;
    }

    if (xor_best_metric < get_x_block_best())
    {
        unsigned gain = get_x_block_best() - xor_best_metric;
        gain *= 3; // use bit estimate (speculative)
        // gain should be greater than overhead for storing
        // reference data: xor token, digest-64, block idx
        unsigned gain_min = unsigned (sizeof(char) + sizeof(bm::id64_t) + sizeof(unsigned));
        gain_min *= 8; // in bits
        if (gain > gain_min)
            return mtype;
    }
    return e_no_xor_match;
}

// --------------------------------------------------------------------------


template<typename BV>
bm::xor_complement_match
xor_scanner<BV>::best_metric(unsigned bc, unsigned gc, unsigned* best_metric)
{
    BM_ASSERT(best_metric);
    unsigned ibc = bm::gap_max_bits - bc;
    if (!ibc)
    {
        *best_metric = gc;
        return e_xor_match_GC;
    }
    if (gc < bc) // GC < GC
    {
        if (gc < ibc)
        {
            *best_metric = gc;
            return e_xor_match_GC;
        }
    }
    else // GC >= BC
    {
        if (bc < ibc)
        {
            *best_metric = bc;
            return e_xor_match_BC;
        }
    }
    *best_metric = ibc;
    return e_xor_match_iBC;
}

// --------------------------------------------------------------------------


} // namespace bm

#endif
