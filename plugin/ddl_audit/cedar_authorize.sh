#!/bin/bash
mysql -u user_alice --socket=build/mysql.sock -e "SELECT * FROM abac_test.sensitive_data;"
mysql -u user_bob --socket=build/mysql.sock -e "SELECT * FROM abac_test.projects;"
mysql -u user_bob --socket=build/mysql.sock -e "INSERT INTO abac_test.projects (id, name, classification) VALUES (4, 'Project A', 'Top Secret');"
mysql -u user_bob --socket=build/mysql.sock -e "UPDATE abac_test.projects SET classification = 'Top Secret' WHERE id = 1;"
mysql -u user_bob --socket=build/mysql.sock -e "DELETE FROM abac_test.projects WHERE id = 1;"
mysql -u user_charlie --socket=build/mysql.sock -e "SELECT * FROM abac_test.employees;"