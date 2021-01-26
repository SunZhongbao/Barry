(global-set-key [(meta ?/)] 'hippie-expand)
(global-set-key (kbd "C-c o") (lambda()(interactive)(shell-command-to-string (format "of %s" (shell-quote-argument (buffer-file-name))))))
(global-set-key (kbd "C-c y n") 'yas-new-snippet)
(global-set-key (kbd "C-c y v") 'yas-visit-snippet-file)
(global-set-key (kbd "C-c y i") 'yas-insert-snippet)

(defun bhj-insert-file-basename () (interactive) (insert (bhj-file-basename)))
(defun bhj-insert-theme () (interactive) (insert (symbol-name (car custom-enabled-themes))))

(global-set-key (kbd "C-c i t") 'bhj-insert-theme)

(defun bhj-insert-date (arg)
  (interactive "p")
  (insert
   (shell-command-to-string
    (if (not arg)
        "today"
      (concat "today " (shell-quote-argument (completing-read "What arg to today? " nil)))))))

(defun bhj-insert-file-basename-sans-extension () (interactive) (insert (file-name-sans-extension (bhj-file-basename))))
(defun bhj-insert-line-number () (interactive) (insert (format "%d" (line-number-at-pos))))
(global-set-key (kbd "C-c i f") 'bhj-insert-file-basename)
(global-set-key (kbd "C-c i d") 'bhj-insert-date)
(global-set-key (kbd "C-c i F") 'bhj-insert-file-basename-sans-extension)
(global-set-key (kbd "C-c i l") 'bhj-insert-line-number)
(global-set-key [(control c) ??] 'bhj-c-show-current-func)
(global-set-key (kbd "C-x C-b") 'ibuffer)
(global-set-key (kbd "C-x b") 'helm-buffers-list)
(global-set-key [(control c)(k)] 'browse-kill-ring)
(global-set-key [(f3)] (lambda()(interactive)(woman (current-word))))
(global-set-key [(control meta shift g)]
                (lambda()(interactive)
                  (let
                      ((search-string (current-word)))
                    (progn
                      (setq search-string
                            (read-string (format "search google with [%s]: " search-string) nil nil search-string))
                      (call-process "bash" nil nil nil "googleemacs.sh" search-string)))))

(global-set-key [(meta n)] 'next-error)

(global-set-key [(meta p)] 'previous-error)

(global-set-key [(meta s) ?p] 'grep-find-file)

(global-set-key [(control meta o)] 'bhj-occur)

(global-set-key (kbd "M-g o") 'bhj-occur)

(global-set-key [f3] 'switch-to-devenv)

(global-set-key [f9] 'devenv-toggle-breakpoint)

(global-set-key [(control f5)] 'devenv-debug)

(global-set-key [(control x) (w)] 'where-are-we)
(global-set-key (kbd "C-x r") (make-sparse-keymap))
(global-set-key (kbd "C-x r r") 'random-theme)
(global-set-key [(control h) (h)] 'bhj-help-it)
(global-set-key [(control h) (q)] 'bhj-help-qt)

(global-set-key [(control x) (c)] 'visit-code-reading)

(global-set-key [(meta shift ? )] 'indent-same-space-as-prev-line)

(global-set-key [(ctrl x) ? ] 'switch-buffer-same-filename)

(global-set-key [(ctrl x) ?\S- ] 'switch-buffer-same-filename-rev)

(global-set-key "\C-cl" 'org-store-link)

(global-set-key "\C-ca" 'org-agenda)
; (global-set-key "\C-ccc" 'org-capture)

(global-set-key "\C-cb" 'org-iswitchb)

(global-set-key (kbd "C-;") 'query-replace-regexp)

(global-set-key [(control x) (control z)] 'keyboard-quit)

(global-set-key (kbd "s-h") help-map)

(global-set-key (kbd "M-.") 'grep-beatags)

(global-set-key (kbd "M-g f") 'grep-func-call)

(global-set-key (kbd "M-s e") 'bhj-occur-make-errors)
(global-set-key (kbd "M-s E") 'bhj-occur-logcat-errors)
(global-set-key (kbd "M-s m") 'bhj-occur-merge-conflicts)

(global-set-key (kbd "M-s x") 'compilation-minor-mode)

(global-set-key [?\C-'] 'hippie-expand)

(global-set-key (kbd "M-s g") 'bhj-do-code-generation)

(global-set-key (kbd "M-s c") (lambda () (interactive) (call-interactively 'compile)))

(global-set-key (kbd "M-g i") 'imenu)

(global-set-key (kbd "M-g m") 'man)

(global-set-key [(meta shift ?d)] 'insert-today)

(define-key minibuffer-local-map [(meta shift return)] 'bhj-find-missing-file)
; (global-set-key [(meta shift return)] 'bhj-find-missing-file)
; (define-key esc-map [(shift return)] 'bhj-find-missing-file)
(define-key minibuffer-local-map [(meta shift f)] 'bhj-clt-insert-file-name)
(define-key minibuffer-local-map [(meta shift e)] 'bhj-edit-grep-pattern)
(define-key minibuffer-local-map [(meta shift d )] 'bhj-insert-pwdu)



(global-set-key [(meta s) ?h ?h] 'source-code-help)

(global-set-key [(control |)] 'toggle-input-method)
(global-set-key [(super ?\\)] 'toggle-input-method)
(define-key global-map [remap upcase-region] 'bhj-upcase-symbol-or-region)
(define-key global-map [remap downcase-region] 'bhj-downcase-symbol-or-region)
(define-key global-map [(super f1)] 'x-maximize-frame)
(define-key global-map [(control x) (b)] 'ivy-switch-buffer)
(define-key global-map [(control c) (W)] 'bhj-set-working-buffer)
(define-key global-map [(meta shift o)] '(lambda () (interactive) (font-lock-fontify-block (line-number-at-pos))))
(when window-system
  (define-key global-map [(control z)] 'undo-tree-undo))
(define-key global-map [(super ?*)] 'enter-emoji)
(global-set-key (kbd "C-M-j") 'skk-mode)
(global-set-key [(f5)] 'evil-mode)
(define-key global-map (kbd "<S-escape>") esc-map)
(define-key global-map (kbd "<XF86Search>") 's-dicts)
(define-key global-map (kbd "<C-M-mouse-1>") 'netease-music-jump-into)
(define-key global-map (kbd "<f12>") 'bhj-wrench-post)
(global-set-key "\C-w" 'backward-kill-word)
(global-set-key "\C-x\C-k" 'kill-region)
(global-set-key "\C-c\C-k" 'kill-region)
