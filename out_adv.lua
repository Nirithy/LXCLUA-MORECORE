local lisp_runner = require("lisp_glr")
local lisp_code = "(progn\
(setq x 0)\
(while (< x 3) (progn\
  (print \"Loop\" x)\
(setq x (+ x 1))\
))\
(setq add (lambda (a b) (progn\
  (return (+ a b))\
)))\
(setq res (add 10 20))\
(print \"Result of add:\" res)\
(goto skip)\
(print \"This should not print\")\
(label skip)\
(print \"Goto success!\")\
)"
-- Execute the transpiled lisp code
lisp_runner.run(lisp_code)
