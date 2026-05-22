-- Test schema creation
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    age INTEGER
);

-- Insert test data
INSERT INTO users VALUES (1, 'Alice', 30);
INSERT INTO users VALUES (2, 'Bob', 25);
INSERT INTO users VALUES (3, 'Charlie', 35);

-- Query data
SELECT * FROM users;
SELECT name, age FROM users WHERE age > 25;

-- Transaction
BEGIN;
COMMIT;
ROLLBACK;
