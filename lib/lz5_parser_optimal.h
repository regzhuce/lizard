#define LZ5_LOG_PARSER(fmt, ...) //printf(fmt, __VA_ARGS__)
#define LZ5_LOG_PRICE(fmt, ...) //printf(fmt, __VA_ARGS__)
#define LZ5_LOG_ENCODE(fmt, ...) //printf(fmt, __VA_ARGS__) 

#define LZ5_OPTIMAL_MIN_OFFSET  8
#define LZ5_OPT_NUM             (1<<12) 
#define REPMINMATCH             1


FORCE_INLINE size_t LZ5_get_price(LZ5_stream_t* const ctx, size_t litLength, U32 offset, size_t matchLength)
{
    if (ctx->params.decompressType == LZ5_coderwords_LZ4)
        return LZ5_get_price_LZ4(ctx, litLength, offset, matchLength);

    return LZ5_get_price_LZ5v2(ctx, litLength, offset, matchLength);
}



typedef struct
{
    int off;
    int len;
    int back;
} LZ5_match_t;

typedef struct
{
    int price;
    int off;
    int mlen;
    int litlen;
    int rep;
} LZ5_optimal_t; 


/* Update chains up to ip (excluded) */
FORCE_INLINE void LZ5_BinTree_Insert(LZ5_stream_t* ctx, const BYTE* ip)
{
#if MINMATCH == 3
    U32* HashTable3  = ctx->hashTable3;
    const BYTE* const base = ctx->base;
    const U32 target = (U32)(ip - base);
    U32 idx = ctx->nextToUpdate;
    
    while(idx < target) {
        HashTable3[LZ5_hash3Ptr(base+idx, ctx->params.hashLog3)] = idx;
        idx++;
    }

    ctx->nextToUpdate = target;
#else
    (void)ctx; (void)ip;
#endif
}



FORCE_INLINE int LZ5_GetAllMatches (
    LZ5_stream_t* ctx,
    const BYTE* const ip,
    const BYTE* const iLowLimit,
    const BYTE* const iHighLimit,
    size_t best_mlen,
    LZ5_match_t* matches)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const BYTE* const dictBase = ctx->dictBase;
    const intptr_t dictLimit = ctx->dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const intptr_t maxDistance = (1 << ctx->params.windowLog) - 1;
    const intptr_t current = (ip - base);
    const intptr_t lowLimit = ((intptr_t)ctx->lowLimit + maxDistance >= current) ? (intptr_t)ctx->lowLimit : current - maxDistance;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const BYTE* match, *matchDict;
    const size_t minMatchLongOff = ctx->params.minMatchLongOff;
    intptr_t matchIndex;
    int nbAttempts = ctx->params.searchNum;
 //   bool fullSearch = (ctx->params.fullSearch >= 2);
    int mnum = 0;
    U32* HashPos;
    size_t mlt;

    if (ip + MINMATCH > iHighLimit) return 0;

    /* First Match */
    HashPos = &HashTable[LZ5_hashPtr(ip, ctx->params.hashLog, ctx->params.searchLength)];
    matchIndex = *HashPos;
#if MINMATCH == 3
    {
    U32* const HashTable3 = ctx->hashTable3;
    U32* HashPos3 = &HashTable3[LZ5_hash3Ptr(ip, ctx->params.hashLog3)];

    if ((*HashPos3 < current) && (*HashPos3 >= lowLimit)) {
        size_t offset = current - *HashPos3;
        if (offset < LZ5_MAX_8BIT_OFFSET) {
            match = ip - offset;
            if (match > base && MEM_readMINMATCH(ip) == MEM_readMINMATCH(match)) {
                size_t mlt = LZ5_count(ip + MINMATCH, match + MINMATCH, iHighLimit) + MINMATCH;

                int back = 0;
                while ((ip + back > iLowLimit) && (match + back > lowPrefixPtr) && (ip[back - 1] == match[back - 1])) back--;
                mlt -= back;

                matches[mnum].off = (int)offset;
                matches[mnum].len = (int)mlt;
                matches[mnum].back = -back;
                mnum++;
            }
        }
    }

    *HashPos3 = current;
    }
#endif

    chainTable[current & contentMask] = (U32)(current - matchIndex);
    *HashPos =  (U32)current;
    ctx->nextToUpdate++;

    if (best_mlen < MINMATCH-1) best_mlen = MINMATCH-1;

    while ((matchIndex < current) && (matchIndex >= lowLimit) && (nbAttempts)) {
        nbAttempts--;
        match = base + matchIndex;
        if ((U32)(ip - match) >= LZ5_OPTIMAL_MIN_OFFSET) {
            if (matchIndex >= dictLimit) {
                if ((/*fullSearch ||*/ ip[best_mlen] == match[best_mlen]) && (MEM_readMINMATCH(match) == MEM_readMINMATCH(ip))) {
                    int back = 0;
                    mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iHighLimit) + MINMATCH;
                    while ((ip+back > iLowLimit) && (match+back > lowPrefixPtr) && (ip[back-1] == match[back-1])) back--;
                    mlt -= back;

                    if ((mlt >= minMatchLongOff) || ((U32)(ip - match) < LZ5_MAX_16BIT_OFFSET))
                    if (mlt > best_mlen) {
                        best_mlen = mlt;
                        matches[mnum].off = (int)(ip - match);
                        matches[mnum].len = (int)mlt;
                        matches[mnum].back = -back;
                        mnum++;

                        if (best_mlen > LZ5_OPT_NUM) break;
                    }
                }
            } else {
                matchDict = dictBase + matchIndex;
    //            fprintf(stderr, "dictBase[%p]+matchIndex[%d]=match[%p] dictLimit=%d base=%p ip=%p iLimit=%p off=%d\n", dictBase, matchIndex, match, dictLimit, base, ip, iLimit, (U32)(ip-match));
                if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
                if (MEM_readMINMATCH(matchDict) == MEM_readMINMATCH(ip)) {
                    int back=0;
                    mlt = LZ5_count_2segments(ip+MINMATCH, matchDict+MINMATCH, iHighLimit, dictEnd, lowPrefixPtr) + MINMATCH;
                    while ((ip+back > iLowLimit) && (matchIndex+back > lowLimit) && (ip[back-1] == matchDict[back-1])) back--;
                    mlt -= back;

                    if ((mlt >= minMatchLongOff) || ((U32)(ip - match) < LZ5_MAX_16BIT_OFFSET))
                    if (mlt > best_mlen) {
                        best_mlen = mlt;
                        matches[mnum].off = (int)(ip - match);
                        matches[mnum].len = (int)mlt;
                        matches[mnum].back = -back;
                        mnum++;
                        
                        if (best_mlen > LZ5_OPT_NUM) break;
                    }
                }
            }
        }
        matchIndex -= chainTable[matchIndex & contentMask];
    }

    return mnum;
}




FORCE_INLINE int LZ5_BinTree_GetAllMatches (
    LZ5_stream_t* ctx,
    const BYTE* const ip,
    const BYTE* const iHighLimit,
    size_t best_mlen,
    LZ5_match_t* matches)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const intptr_t dictLimit = ctx->dictLimit;
    const BYTE* const dictBase = ctx->dictBase;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const intptr_t maxDistance = (1 << ctx->params.windowLog) - 1;
    const intptr_t current = (ip - base);
    const intptr_t lowLimit = ((intptr_t)ctx->lowLimit + maxDistance >= current) ? (intptr_t)ctx->lowLimit : current - maxDistance;
    const BYTE* match;
    const size_t minMatchLongOff = ctx->params.minMatchLongOff;
    int nbAttempts = ctx->params.searchNum;
    int mnum = 0;
    U32 *ptr0, *ptr1, delta0, delta1;
    intptr_t matchIndex;
    size_t mlt = 0;
    U32* HashPos;

    if (ip + MINMATCH > iHighLimit) return 0;

    /* First Match */
    HashPos = &HashTable[LZ5_hashPtr(ip, ctx->params.hashLog, ctx->params.searchLength)];
    matchIndex = *HashPos;

    
#if MINMATCH == 3
    {
    U32* HashPos3 = &ctx->hashTable3[LZ5_hash3Ptr(ip, ctx->params.hashLog3)];

    if ((*HashPos3 < current) && (*HashPos3 >= lowLimit)) {
        size_t offset = current - *HashPos3;
        if (offset < LZ5_MAX_8BIT_OFFSET) {
            match = ip - offset;
            if (match > base && MEM_readMINMATCH(ip) == MEM_readMINMATCH(match))
            {
                mlt = LZ5_count(ip + MINMATCH, match + MINMATCH, iHighLimit) + MINMATCH;

                matches[mnum].off = (int)offset;
                matches[mnum].len = (int)mlt;
                matches[mnum].back = 0;
                mnum++;
            }
        }
        *HashPos3 = current;
    }
    }
#endif
    

    *HashPos = (U32)current;
    ctx->nextToUpdate++;

    // check rest of matches
    ptr0 = &chainTable[(current*2+1) & contentMask];
    ptr1 = &chainTable[(current*2) & contentMask];
    delta0 = delta1 = (U32)(current - matchIndex);

    if (best_mlen < MINMATCH-1) best_mlen = MINMATCH-1;

    while ((matchIndex < current) && (matchIndex >= lowLimit) && (nbAttempts)) {
        mlt = 0;
        nbAttempts--;
        if (matchIndex >= dictLimit) {
            match = base + matchIndex;
            if (MEM_readMINMATCH(match) == MEM_readMINMATCH(ip)) {
                mlt = MINMATCH + LZ5_count(ip+MINMATCH, match+MINMATCH, iHighLimit);
                if ((U32)(ip - match) >= LZ5_OPTIMAL_MIN_OFFSET)
                if ((mlt >= minMatchLongOff) || ((U32)(ip - match) < LZ5_MAX_16BIT_OFFSET))
                if (mlt > best_mlen) {
                    best_mlen = mlt;
                    matches[mnum].off = (int)(ip - match);
                    matches[mnum].len = (int)mlt;
                    matches[mnum].back = 0;
                    mnum++;

                    if (mlt > LZ5_OPT_NUM) break;
                    if (ip + mlt >= iHighLimit) break;
                }
            }
        } else {
            U32 offset = (U32)(ip - (base + matchIndex));
            match = dictBase + matchIndex;
            if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
            if (MEM_readMINMATCH(match) == MEM_readMINMATCH(ip)) {
                mlt = LZ5_count_2segments(ip+MINMATCH, match+MINMATCH, iHighLimit, dictEnd, lowPrefixPtr) + MINMATCH;
                if (offset >= LZ5_OPTIMAL_MIN_OFFSET)
                if ((mlt >= minMatchLongOff) || (offset < LZ5_MAX_16BIT_OFFSET))
                if (mlt > best_mlen) {
                    best_mlen = mlt;
                    matches[mnum].off = (int)offset;
                    matches[mnum].len = (int)mlt;
                    matches[mnum].back = 0;
                    mnum++;

                    if (mlt > LZ5_OPT_NUM) break;
                    if (ip + mlt >= iHighLimit) break;
                }

                if (matchIndex + (int)mlt >= dictLimit) 
                    match = base + matchIndex;   /* to prepare for next usage of match[mlt] */ 
            }
        }

        if (ip[mlt] < match[mlt]) {
            *ptr0 = delta0;
            ptr0 = &chainTable[(matchIndex*2) & contentMask];
            if (*ptr0 == (U32)-1) break;
            delta0 = *ptr0;
            delta1 += delta0;
            matchIndex -= delta0;
        } else {
            *ptr1 = delta1;
            ptr1 = &chainTable[(matchIndex*2+1) & contentMask];
            if (*ptr1 == (U32)-1) break;
            delta1 = *ptr1;
            delta0 += delta1;
            matchIndex -= delta1;
        }
    }

    *ptr0 = (U32)-1;
    *ptr1 = (U32)-1;

    return mnum;
}


#define SET_PRICE(pos, mlen, offset, litlen, price)   \
    {                                                 \
        while (last_pos < pos)  { opt[last_pos+1].price = LZ5_MAX_PRICE; last_pos++; } \
        opt[pos].mlen = (int)mlen;                         \
        opt[pos].off = (int)offset;                        \
        opt[pos].litlen = (int)litlen;                     \
        opt[pos].price = (int)price;                       \
        LZ5_LOG_PARSER("%d: SET price[%d/%d]=%d litlen=%d len=%d off=%d\n", (int)(inr-source), pos, last_pos, opt[pos].price, opt[pos].litlen, opt[pos].mlen, opt[pos].off); \
    }


FORCE_INLINE int LZ5_compress_optimalPrice(
        LZ5_stream_t* const ctx,
        const BYTE* ip,
        const BYTE* const iend)
{
    LZ5_optimal_t opt[LZ5_OPT_NUM + 4];
    LZ5_match_t matches[LZ5_OPT_NUM + 1];
    const BYTE *inr;
    size_t res, cur, cur2, skip_num = 0;
    size_t i, llen, litlen, mlen, best_mlen, price, offset, best_off, match_num, last_pos;

    const BYTE* anchor = ip;
    const BYTE* const mflimit = iend - MFLIMIT;
    const BYTE* const matchlimit = (iend - LASTLITERALS);
    const BYTE* const base = ctx->base;
    const BYTE* const dictBase = ctx->dictBase;
    const intptr_t dictLimit = ctx->dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const intptr_t lowLimit = ctx->lowLimit;
    const intptr_t maxDistance = (1 << ctx->params.windowLog) - 1;

    const size_t sufficient_len = ctx->params.sufficientLength;
    const int faster_get_matches = (ctx->params.fullSearch == 0); 
    const size_t minMatchLongOff = ctx->params.minMatchLongOff;
    const int lz5OptimalMinOffset = (ctx->params.decompressType == LZ5_coderwords_LZ4) ? (1<<30) : LZ5_OPTIMAL_MIN_OFFSET;
    const size_t repMinMatch = (ctx->params.decompressType == LZ5_coderwords_LZ4) ? MINMATCH : REPMINMATCH;

    /* Main Loop */
    while (ip < mflimit) {
        memset(opt, 0, sizeof(LZ5_optimal_t));
        last_pos = 0;
        llen = ip - anchor;

        /* check rep code */

        if (ctx->last_off >= lz5OptimalMinOffset) {
            intptr_t matchIndexLO = (ip - ctx->last_off) - base;
            mlen = 0;
            if ((matchIndexLO >= lowLimit) && (base + matchIndexLO + maxDistance >= ip)) {
                if (matchIndexLO >= dictLimit) {
                    mlen = LZ5_count(ip, base + matchIndexLO, matchlimit);
                } else {
                    mlen = LZ5_count_2segments(ip, dictBase + matchIndexLO, matchlimit, dictEnd, lowPrefixPtr);
                }
            }
            if (mlen >= REPMINMATCH) {
                if (mlen > sufficient_len || mlen >= LZ5_OPT_NUM) {
                    best_mlen = mlen; best_off = 0; cur = 0; last_pos = 1;
                    goto encode;
                }

                do
                {
                    litlen = 0;
                    price = LZ5_get_price(ctx, llen, 0, mlen);
                    if (mlen > last_pos || price < (size_t)opt[mlen].price)
                        SET_PRICE(mlen, mlen, 0, litlen, price);
                    mlen--;
                }
                while (mlen >= REPMINMATCH);
            }
        }

        if (faster_get_matches && last_pos)
           match_num = 0;
        else
        {
            if (ctx->params.parserType == LZ5_parser_optimalPrice) {
                LZ5_Insert(ctx, ip);
                match_num = LZ5_GetAllMatches(ctx, ip, ip, matchlimit, last_pos, matches);
            } else {
                LZ5_BinTree_Insert(ctx, ip);
                match_num = LZ5_BinTree_GetAllMatches(ctx, ip, matchlimit, last_pos, matches);
            }
        }

        LZ5_LOG_PARSER("%d: match_num=%d last_pos=%d\n", (int)(ip-source), match_num, last_pos);
        if (!last_pos && !match_num) { ip++; continue; }

        if (match_num && (size_t)matches[match_num-1].len > sufficient_len) {
            best_mlen = matches[match_num-1].len;
            best_off = matches[match_num-1].off;
            cur = 0;
            last_pos = 1;
            goto encode;
        }

        // set prices using matches at position = 0
        best_mlen = (last_pos > MINMATCH) ? last_pos : MINMATCH;

        for (i = 0; i < match_num; i++) {
            mlen = (i>0) ? (size_t)matches[i-1].len+1 : best_mlen;
            best_mlen = (matches[i].len < LZ5_OPT_NUM) ? matches[i].len : LZ5_OPT_NUM;
            LZ5_LOG_PARSER("%d: start Found mlen=%d off=%d best_mlen=%d last_pos=%d\n", (int)(ip-source), matches[i].len, matches[i].off, best_mlen, last_pos);
            while (mlen <= best_mlen){
                litlen = 0;
                price = LZ5_get_price(ctx, llen + litlen, matches[i].off, mlen);

                if ((mlen >= minMatchLongOff) || (matches[i].off < LZ5_MAX_16BIT_OFFSET))
                if (mlen > last_pos || price < (size_t)opt[mlen].price)
                    SET_PRICE(mlen, mlen, matches[i].off, litlen, price);
                mlen++;
            }
        }

        if (last_pos < repMinMatch) { ip++; continue; }

        opt[0].rep = ctx->last_off;
        opt[0].mlen = 1;
        opt[0].off = -1;

        // check further positions
        for (skip_num = 0, cur = 1; cur <= last_pos; cur++) { 
            inr = ip + cur;

            //           if ((opt[cur-1].mlen == 1))// && (opt[cur-1].off != -1))
            if (opt[cur-1].off == -1){
                litlen = opt[cur-1].litlen + 1;
                
                if (cur != litlen) {
                    price = opt[cur - litlen].price + LZ5_get_price(ctx, litlen, 0, 0);
                    LZ5_LOG_PRICE("%d: TRY1 opt[%d].price=%d price=%d cur=%d litlen=%d\n", (int)(inr-source), cur - litlen, opt[cur - litlen].price, price, cur, litlen);
                } else {
                    price = LZ5_get_price(ctx, llen + litlen, 0, 0);
                    LZ5_LOG_PRICE("%d: TRY2 price=%d cur=%d litlen=%d llen=%d\n", (int)(inr-source), price, cur, litlen, llen);
                }
            } else {
                litlen = 1;
                price = opt[cur - 1].price + LZ5_get_price(ctx, litlen, 0, 0);
                LZ5_LOG_PRICE("%d: TRY3 price=%d cur=%d litlen=%d litonly=%d\n", (int)(inr-source), price, cur, litlen, LZ5_get_price(ctx, litlen, 0, 0));
            }
           
            mlen = 1;
            best_mlen = 0;
            LZ5_LOG_PARSER("%d: TRY price=%d opt[%d].price=%d\n", (int)(inr-source), price, cur, opt[cur].price);

            if (cur > last_pos || price <= (size_t)opt[cur].price) // || ((price == opt[cur].price) && (opt[cur-1].mlen == 1) && (cur != litlen)))
                SET_PRICE(cur, mlen, -1, litlen, price);

            if (cur == last_pos) break;

        //           if (opt[cur].mlen > 1)
            if (opt[cur].off != -1) {
                mlen = opt[cur].mlen;
                offset = opt[cur].off;
                if (offset < 1) {
                    opt[cur].rep = opt[cur-mlen].rep;
                    LZ5_LOG_PARSER("%d: COPYREP1 cur=%d mlen=%d rep=%d\n", (int)(inr-source), cur, mlen, opt[cur-mlen].rep);
                } else {
                    opt[cur].rep = (int)offset;
                    LZ5_LOG_PARSER("%d: COPYREP2 cur=%d offset=%d rep=%d\n", (int)(inr-source), cur, offset, opt[cur].rep);
                }
            } else {
                opt[cur].rep = opt[cur-1].rep; // copy rep
            }


            LZ5_LOG_PARSER("%d: CURRENT price[%d/%d]=%d off=%d mlen=%d litlen=%d rep=%d\n", (int)(inr-source), cur, last_pos, opt[cur].price, opt[cur].off, opt[cur].mlen, opt[cur].litlen, opt[cur].rep); 

            // best_mlen = 0;

            /* check rep code */
            if (opt[cur].rep >= lz5OptimalMinOffset) {
                intptr_t matchIndexLO = (inr - opt[cur].rep) - base;
                mlen = 0;
                if ((matchIndexLO >= lowLimit) && (base + matchIndexLO + maxDistance >= inr)) {
                    if (matchIndexLO >= dictLimit) {
                        mlen = LZ5_count(inr, base + matchIndexLO, matchlimit);
                    } else {
                        mlen = LZ5_count_2segments(inr, dictBase + matchIndexLO, matchlimit, dictEnd, lowPrefixPtr);
                    }
                }
                if (mlen >= REPMINMATCH && mlen > best_mlen) {
                    LZ5_LOG_PARSER("%d: try REP rep=%d mlen=%d\n", (int)(inr-source), opt[cur].rep, mlen);   
                    LZ5_LOG_PARSER("%d: Found REP mlen=%d off=%d rep=%d opt[%d].off=%d\n", (int)(inr-source), mlen, 0, opt[cur].rep, cur, opt[cur].off);

                    if (mlen > sufficient_len || cur + mlen >= LZ5_OPT_NUM) {
                        best_mlen = mlen;
                        best_off = 0;
                        LZ5_LOG_PARSER("%d: REP sufficient_len=%d best_mlen=%d best_off=%d last_pos=%d\n", (int)(inr-source), sufficient_len, best_mlen, best_off, last_pos);
                        last_pos = cur + 1;
                        goto encode;
                    }

                    best_mlen = mlen;
                    if (faster_get_matches)
                        skip_num = best_mlen;

                    do
                    {
                        //if (opt[cur].mlen == 1)
                        if (opt[cur].off == -1) {
                            litlen = opt[cur].litlen;

                            if (cur != litlen) {
                                price = opt[cur - litlen].price + LZ5_get_price(ctx, litlen, 0, mlen);
                                LZ5_LOG_PRICE("%d: TRY1 opt[%d].price=%d price=%d cur=%d litlen=%d\n", (int)(inr-source), cur - litlen, opt[cur - litlen].price, price, cur, litlen);
                            } else {
                                price = LZ5_get_price(ctx, llen + litlen, 0, mlen);
                                LZ5_LOG_PRICE("%d: TRY2 price=%d cur=%d litlen=%d llen=%d\n", (int)(inr-source), price, cur, litlen, llen);
                            }
                        } else {
                            litlen = 0;
                            price = opt[cur].price + LZ5_get_price(ctx, litlen, 0, mlen);
                            LZ5_LOG_PRICE("%d: TRY3 price=%d cur=%d litlen=%d getprice=%d\n", (int)(inr-source), price, cur, litlen, LZ5_get_price(ctx, litlen, 0, mlen - MINMATCH));
                        }

                        LZ5_LOG_PARSER("%d: Found REP mlen=%d off=%d price=%d litlen=%d price[%d]=%d\n", (int)(inr-source), mlen, 0, price, litlen, cur - litlen, opt[cur - litlen].price);

                        if (cur + mlen > last_pos || price <= (size_t)opt[cur + mlen].price) // || ((price == opt[cur + mlen].price) && (opt[cur].mlen == 1) && (cur != litlen))) // at equal price prefer REP instead of MATCH
                            SET_PRICE(cur + mlen, mlen, 0, litlen, price);
                        mlen--;
                    }
                    while (mlen >= REPMINMATCH);
                }
            }

            if (faster_get_matches && skip_num > 0) {
                skip_num--; 
                continue;
            }

            if (ctx->params.parserType == LZ5_parser_optimalPrice) {
                LZ5_Insert(ctx, inr);
                match_num = LZ5_GetAllMatches(ctx, inr, ip, matchlimit, best_mlen, matches);
                LZ5_LOG_PARSER("%d: LZ5_GetAllMatches match_num=%d\n", (int)(inr-source), match_num);
            } else {
                LZ5_BinTree_Insert(ctx, inr);
                match_num = LZ5_BinTree_GetAllMatches(ctx, inr, matchlimit, best_mlen, matches);
                LZ5_LOG_PARSER("%d: LZ5_BinTree_GetAllMatches match_num=%d\n", (int)(inr-source), match_num);
            }


            if (match_num > 0 && (size_t)matches[match_num-1].len > sufficient_len) {
                cur -= matches[match_num-1].back;
                best_mlen = matches[match_num-1].len;
                best_off = matches[match_num-1].off;
                last_pos = cur + 1;
                goto encode;
            }

            // set prices using matches at position = cur
            best_mlen = (best_mlen > MINMATCH) ? best_mlen : MINMATCH;

            for (i = 0; i < match_num; i++) {
                mlen = (i>0) ? (size_t)matches[i-1].len+1 : best_mlen;
                cur2 = cur - matches[i].back;
                best_mlen = (cur2 + matches[i].len < LZ5_OPT_NUM) ? (size_t)matches[i].len : LZ5_OPT_NUM - cur2;
                LZ5_LOG_PARSER("%d: Found1 cur=%d cur2=%d mlen=%d off=%d best_mlen=%d last_pos=%d\n", (int)(inr-source), cur, cur2, matches[i].len, matches[i].off, best_mlen, last_pos);

                if (mlen < (size_t)matches[i].back + 1)
                    mlen = matches[i].back + 1; 

                while (mlen <= best_mlen) {
                  //  if (opt[cur2].mlen == 1)
                    if (opt[cur2].off == -1)
                    {
                        litlen = opt[cur2].litlen;

                        if (cur2 != litlen)
                            price = opt[cur2 - litlen].price + LZ5_get_price(ctx, litlen, matches[i].off, mlen);
                        else
                            price = LZ5_get_price(ctx, llen + litlen, matches[i].off, mlen);
                    } else {
                        litlen = 0;
                        price = opt[cur2].price + LZ5_get_price(ctx, litlen, matches[i].off, mlen);
                    }

                    LZ5_LOG_PARSER("%d: Found2 pred=%d mlen=%d best_mlen=%d off=%d price=%d litlen=%d price[%d]=%d\n", (int)(inr-source), matches[i].back, mlen, best_mlen, matches[i].off, price, litlen, cur - litlen, opt[cur - litlen].price);
        //                if (cur2 + mlen > last_pos || ((matches[i].off != opt[cur2 + mlen].off) && (price < opt[cur2 + mlen].price)))

                    if ((mlen >= minMatchLongOff) || (matches[i].off < LZ5_MAX_16BIT_OFFSET))
                    if (cur2 + mlen > last_pos || price < (size_t)opt[cur2 + mlen].price)
                    {
                        SET_PRICE(cur2 + mlen, mlen, matches[i].off, litlen, price);
                    }

                    mlen++;
                }
            }
        } //  for (skip_num = 0, cur = 1; cur <= last_pos; cur++)


        best_mlen = opt[last_pos].mlen;
        best_off = opt[last_pos].off;
        cur = last_pos - best_mlen;

        encode: // cur, last_pos, best_mlen, best_off have to be set
        for (i = 1; i <= last_pos; i++) {
            LZ5_LOG_PARSER("%d: price[%d/%d]=%d off=%d mlen=%d litlen=%d rep=%d\n", (int)(ip-source+i), i, last_pos, opt[i].price, opt[i].off, opt[i].mlen, opt[i].litlen, opt[i].rep); 
        }

        LZ5_LOG_PARSER("%d: cur=%d/%d best_mlen=%d best_off=%d rep=%d\n", (int)(ip-source+cur), cur, last_pos, best_mlen, best_off, opt[cur].rep); 

        opt[0].mlen = 1;

        while (1) {
            mlen = opt[cur].mlen;
            offset = opt[cur].off;
            opt[cur].mlen = (int)best_mlen; 
            opt[cur].off = (int)best_off;
            best_mlen = mlen;
            best_off = offset;
            if (mlen > cur) break;
            cur -= mlen;
        }
          
        for (i = 0; i <= last_pos;) {
            LZ5_LOG_PARSER("%d: price2[%d/%d]=%d off=%d mlen=%d litlen=%d rep=%d\n", (int)(ip-source+i), i, last_pos, opt[i].price, opt[i].off, opt[i].mlen, opt[i].litlen, opt[i].rep); 
            i += opt[i].mlen;
        }

        cur = 0;

        while (cur < last_pos) {
            LZ5_LOG_PARSER("%d: price3[%d/%d]=%d off=%d mlen=%d litlen=%d rep=%d\n", (int)(ip-source+cur), cur, last_pos, opt[cur].price, opt[cur].off, opt[cur].mlen, opt[cur].litlen, opt[cur].rep); 
            mlen = opt[cur].mlen;
        //            if (mlen == 1) { ip++; cur++; continue; }
            if (opt[cur].off == -1) { ip++; cur++; continue; }
            offset = opt[cur].off;
            cur += mlen;

            LZ5_LOG_ENCODE("%d: ENCODE literals=%d off=%d mlen=%d ", (int)(ip-source), (int)(ip-anchor), (int)(offset), mlen);
            res = LZ5_encodeSequence(ctx, &ip, &anchor, mlen, ip - offset);
            if (res) return 0; 

            LZ5_LOG_PARSER("%d: offset=%d rep=%d\n", (int)(ip-source), offset, ctx->last_off);
        }
    }

    /* Encode Last Literals */
    ip = iend;
    if (LZ5_encodeLastLiterals(ctx, &ip, &anchor)) goto _output_error;

    /* End */
    return 1;
_output_error:
    return 0;
}

