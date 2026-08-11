#pragma once
/* File-level Luke parser: source → Program (Stmt AST). */

#include "luke_ast.hpp"

#include <string>

namespace luke {

/* Parse a Luke source unit (typically already import-expanded) into a Program.
 * Always returns a Program; on failure Program::error is set but partial stmts
 * may still be present for tooling. */
Program parseLuke(const std::string &source);

/* Format a Program back to conversational source (formatter). */
std::string formatProgram(const Program &p);

/* Flatten Program → source text that preserves statement order/bodies.
 * Compile lowers through this so the pipeline is parse → AST → source → emit,
 * with IR dumping the AST. */
std::string flattenProgram(const Program &p);

/* Format a single source file: parse → formatProgram. */
std::string formatLukeSource(const std::string &source);

} // namespace luke
