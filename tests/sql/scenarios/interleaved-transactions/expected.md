Four transactions: T2 (ids 2 and 201), T1 (id 1 twice, 101), T3 (delete id 3, update id 4) and a
last one in session 2 (id 2 again). Expected output order is commit order: T2, T1, T3, then the
last. Each transaction's events are contiguous between its begin and commit message. The
before-image of the second change to id 2 is the value written by the T2 commit, which the
replay check verifies.
