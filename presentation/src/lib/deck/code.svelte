<script lang="ts">
    export let id: string | null = null;
    export let trim = false;
    export let noescape = false;
    export let lines: string | boolean | null = null;
</script>

<pre
    data-id={id}
><code 
    data-trim={trim || null}
    data-line-numbers={lines || null}
    data-noescape={noescape || null}
><slot /></code></pre>
