# Implementation handoff contract

Public API is `src/analysis/relations.h`; SQLite helpers are private in `relations_internal.h`. Storage version/builder version start at 1. Public responses use gfxrecon-query/1. Agents must coordinate changes to these shared declarations rather than fork the API.

## Private SQLite tables (column names are implementation-only)

- `meta(key TEXT PRIMARY KEY,value TEXT)`; values are JSON text. Metadata includes owner=`gfxrecon_gui_relations`, schema=1, builder=1, parser_schema, grouping_version=1, snapshot, complete, source_path/source_sha256, jsonl_path/decoded_sha256, counts and coverage.
- `executions(execution BLOB PRIMARY KEY,block BLOB NOT NULL,occurrence INTEGER NOT NULL,frame INTEGER NOT NULL,pass INTEGER NOT NULL,pipeline_id BLOB,command TEXT NOT NULL,kind TEXT NOT NULL,vertex_count BLOB,index_count BLOB,instance_count BLOB,valid INTEGER NOT NULL,relation_status TEXT NOT NULL,unavailable TEXT NOT NULL,arguments_json TEXT NOT NULL,dynamic_json TEXT NOT NULL,resources_json TEXT NOT NULL,source_json TEXT NOT NULL)`.
- `pipelines(id BLOB PRIMARY KEY,kind TEXT NOT NULL,name TEXT NOT NULL,create_json TEXT NOT NULL,relation_status TEXT NOT NULL)`.
- `shaders(id BLOB PRIMARY KEY,name TEXT NOT NULL)`.
- `pipeline_stages(pipeline_id BLOB NOT NULL,ordinal INTEGER NOT NULL,module_id BLOB,stage TEXT NOT NULL,entry TEXT NOT NULL,specialization_json TEXT NOT NULL,PRIMARY KEY(pipeline_id,ordinal))`.
- `passes(frame INTEGER NOT NULL,pass INTEGER NOT NULL,kind TEXT NOT NULL,submit BLOB NOT NULL,recording_begin BLOB NOT NULL,scope_json TEXT NOT NULL,source_json TEXT NOT NULL,PRIMARY KEY(frame,pass))`.

All captured uint64 IDs and nullable argument counts use exactly eight big-endian bytes. Scalar row counts/frame/ordinal use nonnegative SQLite integers. JSON column values always contain valid serialized JSON (null where appropriate). Stages are normalized by the public NormalizeRelationStage helper. Kinds are draw/dispatch/unknown for executions and graphics/compute/unknown for pipelines. Missing stages/pipeline data yield unresolved or partial relation_status, never complete negative evidence. Add indexes on frame/pass/execution, pipeline/execution, block/occurrence, and module/stage/pipeline reverse edges.

`resources_json` contains compact verified resource references/availability, not full descriptor snapshots or bytes. `source_json` retains API block, jsonl byte offset, submission/recording/invocation identities and logical dump path with explicit not-generated availability. Do not claim a logical dump path exists.

Query output defaults/canonical names are maintained by the field catalog. Base scalar names are execution,block,occurrence,frame,pass,pipeline_id,command,kind,vertex_count,index_count,instance_count,valid,relation_status; entity identities are pipeline_id,shader_id or frame/pass. Structured sections are arguments,shaders,rasterization,resource_refs,provenance for executions; create_info,shaders,rasterization,provenance for pipelines; shader/pass defaults are compact use counts. V1 explicit groupBy is frame/pass, pipeline_id or shader_id (pass grouping includes frame).

Root owns source-cache resolution/EnsureRelationIndex and SQLite/CMake integration. Storage agent owns BuildRelationSnapshot/OpenRelationIndex plus private SQLite helpers. Query agent owns the catalog, validation, QueryRelations and result serialization. CLI agent owns adapters, legacy opt-in projection and ExportRelations. No agent builds until coordinated source freeze.
