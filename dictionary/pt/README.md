# O dicionário português

`words.txt` é o vocabulário que está sempre na máquina para o português:
21,391 palavras, uma por linha, uma tabulação, e as suas categorias nos nomes
do Larry separadas por vírgulas, a mais contada primeiro, ordenadas, em
minúsculas. O Larry lê-o quando aparece uma palavra que nenhuma conceção
ensinou (PLAN.md, A2b), e usa-o para sugerir a palavra a um erro de escrita de
distância de uma palavra desconhecida.

É construído por `scripts/dictionary.sh pt` a partir das formas das palavras
do conjunto de treino do treebank Bosque das Universal Dependencies
(UD_Portuguese-Bosque, licença CC BY-SA 4.0,
https://github.com/UniversalDependencies/UD_Portuguese-Bosque), com as
categorias para que as suas etiquetas se traduzem, como diz o script. Uma
palavra escrita como uma e analisada como duas ("do", "de o") toma a categoria
da primeira parte. Uma categoria vista uma só vez, ou em menos de dois por cento
dos usos da palavra, fica de fora.
