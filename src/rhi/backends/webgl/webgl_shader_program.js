// WebGlShaderProgram/1 cold program component. The RHI factory remains gated
// until P2 supplies authenticated context/resource/command ownership.
export class WebGlShaderProgram {
    #snapshots = new WeakMap();
    #snapshotCount = 0;
    #snapshotBytes = 0;
    #snapshotStorage;
    #snapshotBudget;
    constructor(gl, targets, textureUnits, snapshotBudget) {
        if (!Number.isSafeInteger(snapshotBudget.maxDraws) || snapshotBudget.maxDraws <= 0
            || !Number.isSafeInteger(snapshotBudget.maxBytes) || snapshotBudget.maxBytes <= 0)
            throw Error('SHADER-WEBGL1-SNAPSHOT-BUDGET');
        this.#snapshotBudget = {...snapshotBudget};
        this.#snapshotStorage = new ArrayBuffer(snapshotBudget.maxBytes);
        this.gl = gl;
        this.program = null;
        this.locations = new Map();
        this.leaves = new Map();
        this.targets = targets;
        this.textureUnits = textureUnits;
        this.assess();
        const shaders = [];
        let program = null;
        try {
            for (let i = 0; i < 2; ++i) {
                const shader = gl.createShader(i === 0 ? gl.VERTEX_SHADER : gl.FRAGMENT_SHADER);
                if (!shader) throw Error('SHADER-WEBGL1-NATIVE-SHADER');
                shaders.push(shader);
                gl.shaderSource(shader, targets[i].glslText);
                gl.compileShader(shader);
                if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
                    throw Error('SHADER-WEBGL1-COMPILE: ' + gl.getShaderInfoLog(shader));
            }
            program = gl.createProgram();
            if (!program) throw Error('SHADER-WEBGL1-NATIVE-PROGRAM');
            for (const shader of shaders) gl.attachShader(program, shader);
            for (const a of targets[0].interface.inputs) gl.bindAttribLocation(program, a.location, a.name);
            gl.linkProgram(program);
            if (!gl.getProgramParameter(program, gl.LINK_STATUS))
                throw Error('SHADER-WEBGL1-LINK: ' + gl.getProgramInfoLog(program));
            this.program = program;
            this.validateActiveInterface();
        } catch (error) {
            if (program) gl.deleteProgram(program);
            program = null;
            this.program = null;
            throw error;
        } finally {
            for (const shader of shaders) {
                if (program) gl.detachShader(program, shader);
                gl.deleteShader(shader);
            }
        }
    }
    assess() {
        const gl = this.gl, [vs, fs] = this.targets;
        if (!gl.getParameter(gl.VERSION).startsWith('WebGL 1.0')) throw Error('SHADER-WEBGL1-CONTEXT-VERSION');
        for (const stage of [gl.VERTEX_SHADER, gl.FRAGMENT_SHADER]) {
            const f = gl.getShaderPrecisionFormat(stage, gl.HIGH_FLOAT);
            const i = gl.getShaderPrecisionFormat(stage, gl.HIGH_INT);
            if (!f || !i || f.precision < 16 || f.rangeMin < 62 || f.rangeMax < 62 || i.rangeMin < 16 || i.rangeMax < 16)
                throw Error('SHADER-WEBGL1-HIGHP-UNAVAILABLE');
        }
        for (const target of this.targets) {
            for (const ext of target.requiredExtensions) {
                const name = ext === 'GL_OES_standard_derivatives' ? 'OES_standard_derivatives'
                    : ext === 'GL_EXT_shader_texture_lod' ? 'EXT_shader_texture_lod' : null;
                if (!name || !gl.getExtension(name)) throw Error('SHADER-WEBGL1-REQUIRED-EXTENSION');
            }
        }
        const checks = [[vs.uniformVectors,gl.MAX_VERTEX_UNIFORM_VECTORS], [fs.uniformVectors,gl.MAX_FRAGMENT_UNIFORM_VECTORS],
            [vs.interface.outputs.length,gl.MAX_VARYING_VECTORS], [vs.samplerPairs.length,gl.MAX_VERTEX_TEXTURE_IMAGE_UNITS],
            [fs.samplerPairs.length,gl.MAX_TEXTURE_IMAGE_UNITS], [this.textureUnits.length,gl.MAX_COMBINED_TEXTURE_IMAGE_UNITS]];
        for (const [required, parameter] of checks)
            if (required > gl.getParameter(parameter)) throw Error('SHADER-WEBGL1-QUERIED-LIMIT');
        for (const a of vs.interface.inputs)
            if (a.location >= gl.getParameter(gl.MAX_VERTEX_ATTRIBS)) throw Error('SHADER-WEBGL1-ATTRIBUTE-LIMIT');
    }
    uniformType(leaf) {
        const gl = this.gl;
        if (leaf.columns === 4 && leaf.components === 4 && leaf.scalar === 'Float32') return gl.FLOAT_MAT4;
        if (leaf.columns !== 1 || leaf.components < 1 || leaf.components > 4) throw Error('SHADER-WEBGL1-UNIFORM-TYPE');
        return (leaf.scalar === 'Int32' ? [gl.INT,gl.INT_VEC2,gl.INT_VEC3,gl.INT_VEC4]
            : leaf.scalar === 'Float32' ? [gl.FLOAT,gl.FLOAT_VEC2,gl.FLOAT_VEC3,gl.FLOAT_VEC4] : [])[leaf.components - 1];
    }
    validateActiveInterface() {
        const gl = this.gl, program = this.program;
        const attributes = new Map(this.targets[0].interface.inputs.map(a => [a.name,a]));
        for (let i = 0; i < gl.getProgramParameter(program,gl.ACTIVE_ATTRIBUTES); ++i) {
            const a = gl.getActiveAttrib(program,i), expected = attributes.get(a.name);
            if (!expected || a.size !== 1 || a.type !== this.uniformType({columns:1,components:expected.components,scalar:'Float32'})
                || gl.getAttribLocation(program,a.name) !== expected.location) throw Error('SHADER-WEBGL1-ACTIVE-ATTRIBUTE');
            attributes.delete(a.name);
        }
        if (attributes.size) throw Error('SHADER-WEBGL1-REQUIRED-ATTRIBUTE');
        for (const t of this.targets) for (const block of t.blocks) for (const leaf of block.leaves) {
            const old = this.leaves.get(leaf.name);
            if (old && ['source','set','binding','offset','size','scalar','components','columns','matrixStride'].some(k => old[k] !== leaf[k]))
                throw Error('SHADER-WEBGL1-CROSS-STAGE-UNIFORM');
            this.leaves.set(leaf.name,{...leaf,required:leaf.required || (old && old.required)});
        }
        const expected = new Map([...this.leaves].map(([name,leaf]) => [name,{type:this.uniformType(leaf),required:leaf.required}]));
        for (const unit of this.textureUnits) {
            if (expected.has(unit.name)) throw Error('SHADER-WEBGL1-UNIFORM-ALIAS');
            expected.set(unit.name,{type:gl.SAMPLER_2D,required:true});
        }
        for (let i = 0; i < gl.getProgramParameter(program,gl.ACTIVE_UNIFORMS); ++i) {
            const active = gl.getActiveUniform(program,i);
            if (active.size > 1 && !active.name.endsWith('[0]')) throw Error('SHADER-WEBGL1-ACTIVE-ARRAY');
            for (let j = 0; j < active.size; ++j) {
                const name = active.size > 1 ? active.name.slice(0,-3) + '[' + j + ']' : active.name;
                const e = expected.get(name), location = gl.getUniformLocation(program,name);
                if (!e || e.type !== active.type || location === null || this.locations.has(name))
                    throw Error('SHADER-WEBGL1-ACTIVE-UNIFORM: ' + name);
                this.locations.set(name,location); expected.delete(name);
            }
        }
        for (const [name,e] of expected) if (e.required) throw Error('SHADER-WEBGL1-REQUIRED-UNIFORM: ' + name);
        gl.useProgram(program);
        for (const unit of this.textureUnits) gl.uniform1i(this.locations.get(unit.name),unit.unit);
        if (gl.getError() !== gl.NO_ERROR) throw Error('SHADER-WEBGL1-PROGRAM-STATE');
    }
    // Native locations belong only to this linked program. Snapshot preparation
    // creates typed views once; applying a recorded draw creates no new storage.
    prepareSnapshot(blocks) {
        if (this.#snapshotCount >= this.#snapshotBudget.maxDraws) throw Error('SHADER-WEBGL1-SNAPSHOT-DRAW-BUDGET');
        const writes = [];
        let used = this.#snapshotBytes;
        for (const [name,leaf] of this.leaves) {
            if (!this.locations.has(name)) continue; // Only explicitly optional inactive leaves reach here.
            const sources = blocks.filter(b => b.push === (leaf.source === 'Push') && b.set === leaf.set && b.binding === leaf.binding);
            if (sources.length !== 1) throw Error('SHADER-WEBGL1-SNAPSHOT-COVERAGE');
            const bytes = sources[0].bytes;
            if (!(bytes instanceof Uint8Array) || leaf.offset > bytes.length || leaf.size > bytes.length-leaf.offset
                || (bytes.byteOffset+leaf.offset)%4) throw Error('SHADER-WEBGL1-SNAPSHOT-RANGE');
            if (leaf.size > this.#snapshotBudget.maxBytes - used) throw Error('SHADER-WEBGL1-SNAPSHOT-BUDGET');
            const Type = leaf.scalar === 'Int32' ? Int32Array : Float32Array;
            const values = new Type(this.#snapshotStorage,used,leaf.size/4);
            new Uint8Array(this.#snapshotStorage,used,leaf.size).set(bytes.subarray(leaf.offset,leaf.offset+leaf.size));
            used += leaf.size;
            const method = leaf.columns === 4 ? 'uniformMatrix4fv' : 'uniform'+leaf.components+(leaf.scalar==='Int32'?'iv':'fv');
            writes.push({location:this.locations.get(name),values,method,matrix:leaf.columns===4});
        }
        const key = Object.freeze({});
        this.#snapshots.set(key,writes); this.#snapshotBytes = used; ++this.#snapshotCount;
        return key;
    }
    applySnapshot(snapshot) {
        const writes = this.#snapshots.get(snapshot);
        if (!writes || !this.program) throw Error('SHADER-WEBGL1-SNAPSHOT-PROGRAM');
        const gl = this.gl; gl.useProgram(this.program);
        for (const write of writes) {
            if (write.matrix) gl[write.method](write.location,false,write.values);
            else gl[write.method](write.location,write.values);
        }
    }
    destroy() {
        if (this.program) this.gl.deleteProgram(this.program);
        this.program = null; this.locations.clear();
    }
}
