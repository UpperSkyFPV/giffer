<script lang="ts">
	import Code from "./code.svelte";
	import Markdown from "./markdown.svelte";
	import Slide from "./slide.svelte";
</script>

<Slide>
    <h1>Giffer</h1>
    <h2>Animated GIF generator</h2>
</Slide>


<Slide>
    <Slide>
        <h4>Gerar uma imagem de exemplo</h4>
        <Code>
            ./build/giffer --gen-example \
                --bit-depth=4 \
                --dither \
                -o example.gif
        </Code>

        <div class="flex justify-center">
            <!-- svelte-ignore a11y-missing-attribute -->
            <img data-src="example.gif" />
        </div>
    </Slide>

    <Slide>
        <p>Os artefatos são causados pelo fato que cada pixel utiliza somente <i><b>4bits.</b></i></p>
        <Code lines="2">
            ./build/giffer --gen-example \
                --bit-depth=4 \
                --dither \
                -o example.gif
        </Code>

        <small>Um pixel convencional utiliza 24bits (3 bytes) ou até 32bits (4 bytes) caso haja transparencia.</small>
    </Slide>

    <Slide>
        <p>Caso se use 8bpp, os artefatos não são visíveis.</p>
        <Code>
            ./build/giffer --gen-example --dither -o example-8bpp.gif
        </Code>

        <div class="flex justify-center">
            <!-- svelte-ignore a11y-missing-attribute -->
            <img data-src="example-8bpp.gif" />
        </div>
    </Slide>
</Slide>

<Slide>
    <Slide>
        <h1>O formato GIF</h1>

        <p>
            Na tradição da maioria dos formatos de arquivos, os primeiros bytes são reservados para
            um identificador do formato. No caso do GIF são os bytes:
        </p>

        <Code>GIF87a</Code>
    </Slide>

    <Slide>
        <h1>O <code>Screen Descriptor</code></h1>

        <p>
            O <code>Screen Descriptor</code> é uma seção que define parametros globais da imagem,
            como largura, altura, BPP e cor de fundo.
        </p>

        <p>
            A maioria dos parametros não é utilizado já que um <code>descriptor</code> invidual
            é dado para cada <i>frame</i> na implementação.
        </p>
    </Slide>

    <Slide>
        <h1>O Mapa Global de Cores</h1>
        <small>Global Color Map</small>

        <p>
            Como o formato GIF é paletizado, ou seja, utiliza um conjunto de cores indexadas em
            um mapa ao invés de pixels RGB individuais, é preciso se informar quais são as cores
            disponíveis.
        </p>

        <p>
            Esta não é realmente usada na implementação, já que se usa um mapa individual por
            <i>frame</i>.
        </p>
    </Slide>

    <Slide>
        <h1>O <code>Image Descriptor</code></h1>

        <p>
            Marcado pelo byte <code>','</code>, o <code>Image Descriptor</code> define valores específicos
            parecidos com o <code>Global Descriptor</code>, mas somente para o próximo <i>frame</i>.
        </p>
    </Slide>

    <Slide>
        <h1>O Mapa Local de Cores</h1>

        <p>
            Exatamente igual ao <b>mapa global</b>, mas diz somente quanto ao próximo frame.
        </p>
    </Slide>

    <Slide>
        <h1>Dados</h1>
        <small><code>Raster Data</code></small>

        <p>
            Contém os dados dos pixels da imagem, usando compressão LZW.
        </p>
    </Slide>

    <Slide>
        <h1>Terminador</h1>

        <p>
            O arquivo possui como terminador o caracter <code>';'</code>
        </p>
    </Slide>
</Slide>

<Slide>
    <Slide>
        <h1>A implementação</h1>
    </Slide>

    <Slide>
        <p>
            Todo o código foi implementado em <code>C++</code> sem usar nenhuma biblioteca externa na implementação
            do formato GIF.
        </p>

        <p>
            No entanto, para a leitura dos arquivos de entrada para conversão, foi usada a
            biblioteca <code>stb_image.h</code> e para o processamento dos argumentos do
            programa foi usada a biblioteca <code>CLI11</code>.
        </p>
    </Slide>
</Slide>

