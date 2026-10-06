Regression test for the SIGTERM part of fork fix/exit-code-on-error. The client receives the
transaction, then OLR is stopped with SIGTERM like a container stop. A fixed build logs
"caught signal: 15" and exits 0 after its final checkpoint; upstream 2.0.0 does not handle SIGTERM
(as PID 1 it is ignored) and is killed after the 20 s grace period.
