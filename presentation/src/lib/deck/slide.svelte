<script lang="ts"   >
    export let id: string | null = null;
    export let animate = false;
    export let restart = false;
    export let background: string | null = null;
    export let backgroundRepeat = false;
    export let backgroundSize: string | null = null;
    export let backgroundTransition: string | null = null;
</script>

<section
    data-auto-animate-id={id}
    data-background={background}
    data-background-repeat={backgroundRepeat}
    data-background-size={backgroundSize}
    data-background-transition={backgroundTransition}
    data-auto-animate={animate || null}
    data-auto-animate-restart={restart || null}
>
    <slot />
</section>
