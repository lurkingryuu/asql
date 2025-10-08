### Overview
The `/data` routes expose CRUD-style endpoints for managing Cedar entities and their attributes. All routes are protected by an optional API key header (if the agent is started with authentication), and produce/consume JSON.

- Base path: various under `/data`
- Auth: optional API key via header `Authorization: <token>` (required only if the agent was started with authentication configured)
- Content type: `application/json`
- Data model basics:
    - An entity is represented in Cedar JSON form:
      ```json
      {
        "uid": { "id": "<ENTITY_ID>", "type": "<ENTITY_TYPE>" },
        "attrs": { /* name→value map (strings in current attribute helpers) */ },
        "parents": [ /* optional relationships */ ]
      }
      ```
    - Bulk container type `Entities` is a JSON array of the above entity objects.

### Authentication
- Header: `Authorization: <token>`
- If the agent is configured with a token, the provided header must equal that token, otherwise requests receive `401 Unauthorized`.
- If no authentication is configured, the routes are open.

### Error model
- `400 Bad Request`: schema/validation errors, duplicate entity creation, or general update errors.
- `401 Unauthorized`: missing/invalid API key when authentication is enabled.
- `204 No Content`: on successful deletions.

Concrete error payloads follow project-wide error schema (e.g., `{ "error": "BadRequest", "reason": "..." }`).

---

### GET /data
Retrieve all entities currently stored.

- Auth: `Authorization` header (optional depending on config)
- Request: none
- Responses:
    - `200 OK`: JSON array of entities (`Entities`)
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -H "Authorization: $TOKEN" \
  http://localhost:8000/data
```

Sample response
```json
[
  {
    "uid": { "id": "doc1", "type": "Document" },
    "attrs": { "title": "Report" },
    "parents": []
  }
]
```

---

### PUT /data
Replace the entire entity set with the provided entities (bulk upsert). Entities are validated against the current Cedar schema.

- Auth: `Authorization` header (optional depending on config)
- Request body (`Entities`): JSON array of entity objects
- Responses:
    - `200 OK`: updated full entity list (`Entities`)
    - `400 Bad Request`: validation or conversion errors
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X PUT -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '[{"uid":{"id":"doc1","type":"Document"},"attrs":{"title":"Report"},"parents":[]}]' \
  http://localhost:8000/data
```

Notes
- This endpoint writes the entire set; use carefully to avoid unintentionally removing items not in your submission.

---

### DELETE /data
Delete all entities.

- Auth: `Authorization` header (optional depending on config)
- Request: none
- Responses:
    - `204 No Content` on success
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X DELETE -H "Authorization: $TOKEN" http://localhost:8000/data
```

---

### PUT /data/entity
Create a single, empty entity by type and id. Fails if the entity already exists. Attributes are initialized to an empty object and parents to an empty list.

- Auth: `Authorization` header (optional depending on config)
- Request body (`NewEntity`):
  ```json
  { "entity_type": "<TYPE>", "entity_id": "<ID>" }
  ```
- Responses:
    - `200 OK`: full entities after insertion (`Entities`)
    - `400 Bad Request`: duplicate entity or validation error
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X PUT -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '{"entity_type":"Document","entity_id":"doc42"}' \
  http://localhost:8000/data/entity
```

Behavior details
- The server builds the entity as:
  ```json
  {"uid":{"id":"<ID>","type":"<TYPE>"},"attrs":{},"parents":[]}
  ```
- It checks for an existing entity with the same uid before adding. On conflict returns a duplicate error.

---

### PUT /data/attribute
Add or update a string attribute on an existing entity.

- Auth: `Authorization` header (optional depending on config)
- Request body (`EntityAttributeWithValue`):
  ```json
  {
    "entity_type": "<TYPE>",
    "entity_id": "<ID>",
    "attribute_name": "<ATTR_NAME>",
    "attribute_value": "<ATTR_VALUE>"
  }
  ```
- Responses:
    - `200 OK`: the updated entity (`Entity`)
    - `400 Bad Request`: entity not found or validation error
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X PUT -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '{"entity_type":"Document","entity_id":"doc42","attribute_name":"title","attribute_value":"Quarterly"}' \
  http://localhost:8000/data/attribute
```

Notes
- Current helper treats attribute values as strings. Ensure your Cedar schema is compatible with string values for the given attribute.

---

### DELETE /data/attribute
Remove an attribute from an existing entity.

- Auth: `Authorization` header (optional depending on config)
- Request body (`EntityAttribute`):
  ```json
  {
    "entity_type": "<TYPE>",
    "entity_id": "<ID>",
    "attribute_name": "<ATTR_NAME>"
  }
  ```
- Responses:
    - `200 OK`: the updated entity (`Entity`)
    - `400 Bad Request`: entity not found or validation error
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X DELETE -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '{"entity_type":"Document","entity_id":"doc42","attribute_name":"title"}' \
  http://localhost:8000/data/attribute
```

---

### PUT /data/single
Append exactly one entity to the existing set (no duplicate check). The request still uses the bulk `Entities` array shape, but only the last element is used.

- Auth: `Authorization` header (optional depending on config)
- Request body (`Entities`): JSON array; only the last entity in the array is considered
- Responses:
    - `200 OK`: full entity list after insertion (`Entities`)
    - `400 Bad Request`: validation or conversion errors
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X PUT -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '[{"uid":{"id":"u1","type":"User"},"attrs":{},"parents":[]}]' \
  http://localhost:8000/data/single
```

Notes
- Unlike `/data/entity`, this endpoint does not check for duplicates; if you send an entity with an existing uid, behavior depends on Cedar validation and subsequent state update (may result in multiple entries until later operations consolidate or overwrite via `/data/single/<id>`).

---

### PUT /data/single/<entity_id>
Update or insert a single entity by id. If an entity with the given id exists, it is replaced by the provided entity (from the request array’s last item) and the updated entity is returned. If not found, the new entity is appended and that entity is returned.

- Path params:
    - `entity_id`: UID id to match (string)
- Auth: `Authorization` header (optional depending on config)
- Request body (`Entities`): JSON array; the last element is taken as the new entity
- Responses:
    - `200 OK`: the updated or inserted entity (`Entity`)
    - `400 Bad Request`: validation or conversion errors
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X PUT -H "Authorization: $TOKEN" -H "Content-Type: application/json" \
  -d '[{"uid":{"id":"doc42","type":"Document"},"attrs":{"title":"Revised"},"parents":[]}]' \
  http://localhost:8000/data/single/doc42
```

Notes
- Match is performed on `uid.id` only; the provided object should carry the intended `uid.type` as well.

---

### DELETE /data/single/<entity_id>
Remove a single entity by `uid.id`.

- Path params:
    - `entity_id`: UID id to delete (string)
- Auth: `Authorization` header (optional depending on config)
- Request: none
- Responses:
    - `204 No Content`: on successful deletion
    - `400 Bad Request`: validation or update error
    - `401 Unauthorized`: if auth fails

Example
```bash
curl -sS -X DELETE -H "Authorization: $TOKEN" \
  http://localhost:8000/data/single/doc42
```

---

### Data shapes (schemas)
- `Entity` (opaque wrapper around JSON value of a Cedar entity), fields commonly used:
    - `uid.id` (string)
    - `uid.type` (string)
    - `attrs` (object)
    - `parents` (array)
- `Entities`: array of `Entity` values
- `NewEntity`:
  ```json
  { "entity_type": "string", "entity_id": "string" }
  ```
- `EntityAttributeWithValue`:
  ```json
  { "entity_type": "string", "entity_id": "string", "attribute_name": "string", "attribute_value": "string" }
  ```
- `EntityAttribute`:
  ```json
  { "entity_type": "string", "entity_id": "string", "attribute_name": "string" }
  ```

### Additional notes
- All mutations validate against the current Cedar schema fetched from the schema store; invalid entities/attributes produce `400 Bad Request` with details.
- The code compares IDs as strings via `uid.id`; ensure consistent casing and exact matches.
- The attribute helpers currently only set/remove string values. Use the bulk update endpoints if you need to set complex attribute values matching your Cedar schema.
- OpenAPI docs are generated via `#[openapi]` annotations; if you run with Rocket Okapi integration, these routes will appear in the Swagger spec accordingly.
