NAME := paper
BIBTEX := bibtex
LATEX := pdflatex
LATEX_FLAGS := -shell-escape

.PHONY: clean

all: $(NAME).pdf

$(NAME).pdf: $(wildcard *.tex)
	$(LATEX) $(LATEX_FLAGS) $(NAME).tex
	$(LATEX) $(LATEX_FLAGS) $(NAME).tex

clean:
	rm -rf *.{aux,log,out,pdf,toc} _minted-paper
