# xuan-types

TypeScript type definitions for the [xuan](https://github.com/topelinux/xuan) runtime.

To use the typings in your TS project, install it via `npm i xuan-types --save-dev`.

Then either add `node_modules/xuan-types` to the `typeRoots` in your `tsconfig.json`:

```json
"typeRoots": [
	"node_modules/xuan-types",
	"node_modules/@types"
]
```

Or alternatively add `import type from 'xuan-types';` somewhere in your code.
