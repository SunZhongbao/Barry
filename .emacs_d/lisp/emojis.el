;;; Code: utf-8

(defvar emoji-hash-table (make-hash-table :test 'equal) "The hash table for emojis.")
(defvar emoji-names () "The names of the emojis.")
(defvar emoji-history nil "History of emojis input.")
(defvar emoji-regexp-sh-quoted nil "The emojis regexp quoted for shortening")
(defvar emoji-alist nil "The alist for emojis.")

(defun setup-emoji-hash ()
  (interactive)
  (setq emoji-hash-table (make-hash-table :test 'equal)
        emoji-names nil)
  (let* ((wrench-emojis-file "~/src/github/Wrench/release/emojis/emojis.el")
         (wrench-emojis-file-x "~/.local-config/etc/emojis.el"))
    (if (file-exists-p wrench-emojis-file-x)
        (progn
          (load wrench-emojis-file-x)
          (load "~/.local-config/etc/wrench-icons.el"))
      (load wrench-emojis-file)))
  (setq emoji-alist emojis-string-list)
  (let (emoji-regexp-list emoji-regexp)
    (while emojis-string-list
      (let* ((emoji (caar emojis-string-list))
             (key (concat " " emoji " " (caddar emojis-string-list)))
             (png (cadar emojis-string-list))
             (emoji-image-size (floor (* bhj-english-font-size 2.5))))
        (setq emoji-regexp-list (cons emoji emoji-regexp-list))
        (unless (string= png "")
          (setq png (create-image png
                                  (when (fboundp 'imagemagick-types)
                                    'imagemagick)
                                  nil
                                  :ascent 'center
                                  :heuristic-mask t
                                  :height emoji-image-size))
          (add-text-properties 0 (length emoji) `(display ,png) emoji)
          (add-text-properties 1 (1+ (length emoji)) `(display ,png) key))
        (puthash key emoji emoji-hash-table)
        (setq emoji-names (cons key emoji-names)))
      (setq emojis-string-list (cdr emojis-string-list)))
    (setq emoji-regexp (string-join (nreverse (sort emoji-regexp-list #'string<)) "|"))
    (setq emoji-regexp-sh-quoted (shell-quote-argument emoji-regexp)))
  (setq emoji-names (reverse emoji-names)))

(defun active-emoji-regexp ()
  "Run a helper perl script to calculate active emojis in the current buffer as a regexp."
  (shell-command-on-region-to-string (concat "DEBUG_RUN_REEXEC=true debug-run active-emoji-regexp " emoji-regexp-sh-quoted " 2>~/tmp/active-emoji-regexp.log") (point-min) (point-max)))

(defun org2pdf-emojify ()
  (interactive)
  (unless emoji-alist
    (setup-emoji-hash))
  (replace-regexp
   (active-emoji-regexp)
   '(replace-eval-replacement
     replace-quote
     (format
      "\\text{\\includegraphics[width=1em,valign=t,raise=0.1em]{%s}}"
      (expand-file-name (nth 1 (assoc (match-string 0) emoji-alist))))) nil (point-min) (point-max) nil))

(defun org2html-emojify ()
  (interactive)
  (unless emoji-alist
    (setup-emoji-hash))
  (replace-regexp
   (active-emoji-regexp)
   '(replace-eval-replacement
     replace-quote
     (format
      "<img class='emoji' src='%s' style='font-size: 1em; width: 1.3em; vertical-align: -0.35em;' />"
      (expand-file-name (nth 1 (assoc (match-string 0) emoji-alist))))) nil (point-min) (point-max) nil))

;;;###autoload
(defun enter-emoji ()
  "Let the user input an emoji interactively"
  (interactive)
  (unless emoji-names
    (setup-emoji-hash))
  (let ((emoji))
    (flet ((bhj-hack-helm-s-return-helper () (interactive) (throw 's-return (buffer-substring-no-properties (point-min) (point-max)))))
      (let ((key (catch 's-return
                   (let ((ivy-sort-functions-alist nil))
                     (completing-read "Enter your emoji: " emoji-names nil t nil 'emoji-history)))))
        (setq emoji (gethash key emoji-hash-table))
        (if emoji
            (progn
              (setq emoji-names (cons (find-if (lambda (n) (string= key n)) emoji-names) (delete-if (lambda (n) (string= key n)) emoji-names)))
              (insert emoji))
          (setq key (car (split-string key "\n")))
          (setq key (replace-regexp-in-string ".*Enter your emoji: " "" key))
          (let  ((emoji-names-copy (copy-sequence emoji-names))
                 (stems (split-string key "\\s +")))
            (while stems
              (setq emoji-names-copy (delete-if (lambda (n)
                                                  (let* ((stem (car stems))
                                                         (not? (string-match "^!" stem))
                                                         match)
                                                    (when not? (setq stem (substring stem 1)))
                                                    (setq match
                                                          (let ((case-fold-search t))
                                                            (string-match (regexp-quote stem) n)))
                                                    (if not?
                                                        match
                                                      (not match))))
                                                emoji-names-copy))
              (setq stems (cdr stems)))
            (while emoji-names-copy
              (setq key (car emoji-names-copy))
              (setq emoji (gethash key emoji-hash-table))
              (insert emoji)
              (setq emoji-names-copy (cdr emoji-names-copy)))))))))

(provide 'emojis)
;;; emojis.el ends here
