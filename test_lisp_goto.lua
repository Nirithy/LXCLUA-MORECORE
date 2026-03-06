local lisp = require("lisp_glr")
local code = [[
(progn
  (setq i 0)
  (label start)
  (if (< i 3) (progn
    (print "goto loop" i)
    (setq i (+ i 1))
    (goto start)
  ))
  (print "done")
)
]]
lisp.run(code)
