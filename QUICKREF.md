# Quick Reference: Integrated Testing with Real Cedar Agent

## 🚀 Quick Start Commands

### 1. Start Cedar Agent (Terminal 1)
```bash
# Using the helper script
./scripts/start_cedar_agent.sh

# Or manually
./target/release/cedar-agent \
  -l debug \
  -s scripts/cedar_agent_schema.json \
  -d scripts/cedar_agent_data.json \
  --policies scripts/cedar_agent_policies.json \
  --addr 0.0.0.0 --port 8280
```

### 2. Run Integrated Test (Terminal 2)
```bash
# Docker MySQL
./test_integrated_plugins.sh --docker

# Local MySQL
./test_integrated_plugins.sh --mysql-port 3306 --mysql-user root --mysql-password yourpass

# With debug output
./test_integrated_plugins.sh --docker --debug --verbose
```

## 📁 File Locations

### Configuration Files (for Cedar Agent)
- `scripts/cedar_agent_schema.json` - Entity types and actions schema
- `scripts/cedar_agent_data.json` - Test users, tables, databases
- `scripts/cedar_agent_policies.json` - Authorization policies

### Test Scripts
- `test_integrated_plugins.sh` - Main integrated test runner
- `scripts/start_cedar_agent.sh` - Cedar Agent startup helper
- `scripts/define_cedar_policies.sh` - Policy management (optional)

## 🧪 Test Scenarios

The integrated test validates 15 scenarios:

| User | Can Do | Cannot Do |
|------|--------|-----------|
| alice_user | SELECT employees | INSERT/UPDATE/DELETE employees |
| bob_user | SELECT/INSERT/UPDATE projects | Access other tables |
| hr_manager | SELECT/INSERT/UPDATE employees, payroll | DELETE payroll |
| developer | SELECT any table | INSERT/UPDATE/DELETE |
| analyst | SELECT reporting_db | Access company_db |
| admin_user | Everything | Nothing restricted |

## 🔧 Common Operations

### Check Cedar Agent Status
```bash
# Health check
curl http://localhost:8280/

# List policies
curl http://localhost:8280/v1/policies | jq

# List entities
curl http://localhost:8280/v1/data | jq

# Get schema
curl http://localhost:8280/v1/schema | jq
```

### Test Authorization Directly
```bash
curl -X POST http://localhost:8280/v1/is_authorized \
  -H "Content-Type: application/json" \
  -d '{
    "principal": {"type": "User", "id": "alice_user"},
    "action": {"type": "Action", "id": "Select"},
    "resource": {"type": "Table", "id": "employees"},
    "context": {}
  }'
```

### Check MySQL Plugin Status
```sql
-- Check plugins are loaded
SELECT PLUGIN_NAME, PLUGIN_STATUS 
FROM INFORMATION_SCHEMA.PLUGINS 
WHERE PLUGIN_NAME IN ('ddl_audit', 'cedar_authorization');

-- Check configuration
SHOW VARIABLES LIKE 'cedar_authorization%';
SHOW VARIABLES LIKE 'ddl_audit%';

-- Check statistics
SHOW STATUS LIKE 'cedar_authorization%';
```

### Cleanup
```bash
# Run cleanup only
./test_integrated_plugins.sh --docker --cleanup-only

# Restart Cedar Agent (Ctrl+C then restart)
```

## 🐛 Troubleshooting Quick Fixes

### Cedar Agent Won't Start
```bash
# Check if port is in use
lsof -i :8280

# Validate JSON files
jq . scripts/cedar_agent_schema.json
jq . scripts/cedar_agent_data.json
jq . scripts/cedar_agent_policies.json
```

### Plugin Connection Errors
```bash
# Test Cedar Agent connectivity
curl http://localhost:8280/

# Check from MySQL container (if using Docker)
docker compose exec mysql curl http://host.docker.internal:8280/
```

### Tests Failing
```bash
# Run with full debugging
./test_integrated_plugins.sh --docker --debug --verbose

# Check Cedar Agent logs (look at terminal where it's running)

# Verify policies are loaded
curl http://localhost:8280/v1/policies | jq 'length'
```

## 📊 Expected Test Results

```
========================================
STAGE 1: PASS
STAGE 2: PASS
STAGE 3: PASS
STAGE 4: PASS (15/15)
STAGE 5: PASS
========================================
Total Stages: 5
Passed Stages: 5
ALL STAGES COMPLETED SUCCESSFULLY!
```

## 🔑 Key Files Created

1. **Cedar Agent Configuration** (3 files)
   - `scripts/cedar_agent_schema.json` - Defines entity structure
   - `scripts/cedar_agent_data.json` - Test entities/relationships
   - `scripts/cedar_agent_policies.json` - Authorization rules

2. **Test Infrastructure** (3 files)
   - `test_integrated_plugins.sh` - Main test orchestrator
   - `scripts/start_cedar_agent.sh` - Cedar Agent launcher
   - `scripts/define_cedar_policies.sh` - Policy helper (optional)

3. **Documentation**
   - `INTEGRATED_TESTING.md` - Comprehensive guide
   - `QUICKREF.md` - This quick reference

## 💡 Pro Tips

1. **Always start Cedar Agent first** before running tests
2. **Use --debug flag** when tests fail to see detailed output
3. **Check Cedar Agent terminal** for authorization decision logs
4. **Policies are cached** - restart Cedar Agent to reload policy changes
5. **Port 8280** is default - use `--cedar-url` to change

## 🔗 Related Commands

### If using custom Cedar Agent path
```bash
./scripts/start_cedar_agent.sh --cedar-agent ~/path/to/cedar-agent
```

### If using authentication
```bash
# Start Cedar Agent with auth
./target/release/cedar-agent -a mysecret --port 8280 ...

# Run tests with auth
./test_integrated_plugins.sh --docker --cedar-auth-token mysecret
```

### For custom policies
```bash
# Edit the policy file
vim scripts/cedar_agent_policies.json

# Restart Cedar Agent to reload
```

---

**For full documentation, see:** `INTEGRATED_TESTING.md`

