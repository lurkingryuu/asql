SELECT * FROM abac_test.sensitive_data;
SELECT * FROM abac_test.projects;
SELECT * FROM abac_test.employees;
INSERT INTO abac_test.projects (id, name, classification) VALUES (4, 'Project A', 'Top Secret');
UPDATE abac_test.projects SET classification = 'Top Secret' WHERE id = 1;
DELETE FROM abac_test.projects WHERE id = 1;