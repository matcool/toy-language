#pragma once
#include <optional>
#include <vector>
#include "format.hpp"
#include "util.hpp"

enum class TokenType {
	Unknown,
	Semicolon,
	String,
	LeftParen,
	RightParen,
	LeftBracket,
	RightBracket,
	TypeIndicator, // :
	Number,
	Identifier,
	Assign,
	Comma,
	Keyword
};

struct Token {
	TokenType type;
	std::string data;

	size_t line = 0, column = 0;
	// TODO: store origin (file, line, column)

	Token(TokenType type) : type(type) {}
	Token(TokenType type, const std::string& data) : type(type), data(data) {}
};

class Lexer {
public:
	std::istream& m_stream;
	size_t m_line = 1, m_column = 0;

	Lexer(std::istream& stream) : m_stream(stream) {}

	inline void new_line() { ++m_line; m_column = 0; }
	inline void next_char(char c) {
		if (c == '\n')
			new_line();
		else
			++m_column;
	}
	void eat_until(std::string& buffer, char target);
	std::optional<Token> get_token();
	std::vector<Token> get_tokens();
};

struct Type {
	// TODO: enum
	std::string name;
};

struct Variable {
	Type type;
	std::string name;
};

enum class ExpressionType {
	Literal,
	Declaration,
	Variable
};

struct Expression {
	ExpressionType type;
	Type prefer_type = Type { "void" };
	std::vector<Expression> children;
	// TODO: consider dynamic polymorphism instead of this
	struct DeclarationData {
		Variable var;
	};
	struct VariableData {
		std::string name;
	};
	struct LiteralData {
		std::variant<int, std::string> value;
	};
	std::variant<bool, DeclarationData, VariableData, LiteralData> data;
	Expression(ExpressionType type) : type(type) {}
};

enum class StatementType {
	Expression, // statement is just an expression
	Assignment, // statement assigns an expression to another
	Return      // statement returns an expression
};

struct Statement {
	StatementType type;
	std::vector<Expression> expressions;
};

struct Function;

struct Scope {
	std::vector<Variable> variables;
};

struct Function {
	Type return_type;
	std::string name;
	std::vector<Variable> arguments;
	Scope scope;
	std::vector<Statement> statements;
};

class Parser {
public:
	Scope m_global_scope;
	std::vector<Function> m_functions;
	std::string m_file_name;

	Parser() {}
	Parser(const std::string_view& file_name);

	Variable parse_var_decl(ArrayStream<Token>& tokens);
	void parse_function(Function& function, ArrayStream<Token>& tokens);
	Statement parse_statement(ArrayView<Token> tokens, Function* parent);
	Expression parse_expression(ArrayView<Token> tokens, const Type);
	void error_at_token(const Token& token, const std::string_view& msg);
	Token& expect_token_type(Token& token, TokenType type, const std::string_view& msg);
	void parse(ArrayView<Token> tokens);
};

class Compiler {
public:
	struct Identifier {
		size_t n;
		Type type;
	};
	// resets on each function
	size_t m_id_counter = 0;
	std::ostream& m_stream;
	Parser& m_parser;

	Compiler(std::ostream& output, Parser& parser) : m_stream(output), m_parser(parser) {}

	Identifier reserve_var();
	Identifier allocate_var();

	template <class... Args>
	void write_inst(const std::string_view& format, Args&&... args) {
		format_to(m_stream, format, args...);
		m_stream << '\n';
	}

	void compile_expression(Expression&);
	void compile_statement(Statement&);

	void compile();

};