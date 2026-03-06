local lisp = require("lisp_glr")
local code = [[
(progn
  (setq i 0)
  (while (< i 3) (progn
    (print "loop" i)
    (setq i (+ i 1))
  ))

  (setq f (lambda (x) (progn
    (return (* x 2))
  )))
  (print "closure result" (f 5))
)
]]
lisp.run(code)
