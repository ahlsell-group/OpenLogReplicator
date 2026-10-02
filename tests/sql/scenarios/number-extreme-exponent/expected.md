Oracle NUMBER spans 1e-130 to 9.99e125. Expected: the exact value in the create and update
events, not 0.

Observed with OLR 2.0.0 and a build of the same code base from the fork: the 1e-130 values are
written as `0`, so the replayed row differs from the database. Not yet fixed upstream, so this
scenario currently fails with every image (see `known_failing` in scenario.toml).
