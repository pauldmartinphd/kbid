PAPER=paper

.PHONY: all

all:
	$(MAKE) clean
	pdflatex $(PAPER)
	bibtex $(PAPER)
	pdflatex $(PAPER)
	pdflatex $(PAPER)

clean:
	rm -rf $(PAPER).aux $(PAPER).bbl $(PAPER).blg $(PAPER).log $(PAPER).out kbid.pdf
