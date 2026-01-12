# AI Development Workflow Guide

## Philosophy

### Core Principles
- **Incremental Progress**: Small, tested changes over large refactors
- **Verification First**: Always verify current state before making changes
- **Test-Driven**: Write tests before implementation when possible
- **Documentation**: Keep docs in sync with code changes
- **Reversibility**: Make changes that can be easily rolled back

### Communication Style
- Be direct and specific in requests
- Ask clarifying questions rather than making assumptions
- Explain the "why" behind technical decisions
- Push back if something seems wrong or unclear
- Iterate based on feedback

---

## Development Workflow

### 1. Understanding Phase
**Before writing any code:**
- [ ] Read relevant documentation/README
- [ ] Examine existing code structure
- [ ] Understand the tech stack and dependencies
- [ ] Identify testing framework and conventions
- [ ] Note any linting/formatting tools in use

**Questions to ask:**
- What's the current state of this feature/module?
- What are the project's conventions?
- Are there similar patterns elsewhere in the codebase?
- What's the testing strategy?

### 2. Planning Phase
**For each task:**
- [ ] Break down into smallest possible increments
- [ ] Identify what needs to be tested
- [ ] Consider edge cases and error handling
- [ ] Plan for backwards compatibility if relevant
- [ ] Identify files that will be changed

**Output a plan like:**
```
1. Add test for X functionality
2. Implement X in minimal way
3. Verify tests pass
4. Add error handling
5. Update documentation
6. Commit with message: "feat: add X functionality"
```

### 3. Implementation Phase

#### Test-Driven Development
```
1. Write failing test
2. Run test to confirm it fails
3. Write minimal code to pass
4. Run test to confirm it passes
5. Refactor if needed
6. Run test again
```

#### Code Quality Standards
- Follow existing code style (indentation, naming, structure)
- Use meaningful variable and function names
- Keep functions small and focused (< 50 lines ideal)
- Add comments for complex logic, not obvious code
- Handle errors explicitly, don't swallow exceptions

#### Commit Strategy
- One logical change per commit
- Use conventional commits: `type(scope): description`
  - `feat`: new feature
  - `fix`: bug fix
  - `docs`: documentation only
  - `style`: formatting, no code change
  - `refactor`: code restructuring
  - `test`: adding tests
  - `chore`: maintenance tasks

### 4. Verification Phase
**After each change:**
- [ ] Run relevant tests
- [ ] Check for linting errors
- [ ] Verify the feature works as intended
- [ ] Test edge cases manually if needed
- [ ] Review diff before committing

---

## Problem-Solving Approach

### When Stuck
1. **Read error messages carefully** - they usually tell you what's wrong
2. **Check the documentation** - for the library/framework version in use
3. **Look for similar patterns** - in the existing codebase
4. **Simplify** - remove complexity until you find the issue
5. **Search** - but verify solutions match your versions/context

### Debugging Strategy
```
1. Reproduce the issue reliably
2. Form a hypothesis about the cause
3. Test the hypothesis (add logging, breakpoints, etc.)
4. Fix the root cause, not symptoms
5. Add a test to prevent regression
```

### When to Ask for Help
- After trying 2-3 different approaches
- When unsure about architectural decisions
- When impact might affect other parts of the system
- When security or performance implications are unclear

---

## Code Review Self-Checklist

Before presenting code:
- [ ] Code follows project conventions
- [ ] Tests are written and passing
- [ ] No commented-out code (remove or explain)
- [ ] No console.log / print statements (use proper logging)
- [ ] Error cases are handled
- [ ] Documentation is updated
- [ ] No TODOs left without explanation
- [ ] Performance implications considered
- [ ] Security implications considered (input validation, etc.)

---

## Documentation Standards

### Code Comments
```python
# Good: Explains WHY
# Using binary search here because dataset can be 10M+ records
result = binary_search(data, target)

# Bad: Explains WHAT (code already shows this)
# Search for target in data
result = binary_search(data, target)
```

### README Updates
When changing functionality:
- Update installation steps if dependencies change
- Update usage examples if API changes
- Update configuration docs if new options added
- Keep version compatibility notes current

### API Documentation
- Document all public functions/methods
- Include parameter types and return types
- Provide usage examples
- Note any side effects
- Document error cases

---

## Anti-Patterns to Avoid

### Don't:
- ❌ Make sweeping changes without tests
- ❌ Copy-paste code without understanding it
- ❌ Ignore linting errors "just to get it working"
- ❌ Commit broken code with "WIP" messages
- ❌ Add dependencies without justification
- ❌ Over-engineer simple solutions
- ❌ Leave debug code in production

### Do:
- ✅ Make small, testable changes
- ✅ Understand before implementing
- ✅ Fix root causes, not symptoms
- ✅ Keep commits atomic and reversible
- ✅ Evaluate dependencies carefully
- ✅ Choose simplest solution that works
- ✅ Clean up as you go

---

## Technology-Specific Guidelines

### Python
- Follow PEP 8 style guide
- Use type hints for function signatures
- Prefer `pathlib` over `os.path`
- Use context managers for resources
- Virtual environments for dependency isolation

### JavaScript/TypeScript
- Use `const` by default, `let` when needed, never `var`
- Prefer async/await over promise chains
- Use TypeScript strict mode
- Destructure when it improves readability
- Use optional chaining (`?.`) for nested properties

### Git
- Write descriptive commit messages (not "fix" or "update")
- Use branches for features: `feature/descriptive-name`
- Rebase before merging to keep history clean
- Don't force push to shared branches
- Keep commits focused on one thing

---

## Project Initialization Checklist

When starting a new project with AI:
- [ ] Create clear project structure
- [ ] Set up testing framework
- [ ] Configure linting/formatting
- [ ] Create `.gitignore`
- [ ] Write initial README with:
  - Purpose/description
  - Installation steps
  - Basic usage
  - Development setup
- [ ] Add pre-commit hooks if applicable
- [ ] Create `agents.md` for project-specific workflow
- [ ] Set up CI/CD if appropriate

---

## Templates

### agents.md Project Template
```markdown
# [Project Name] - AI Development Guide

## Project Overview
[What this project does and why it exists]

## Architecture
[High-level structure - frontend/backend/database/etc.]

## Tech Stack
- Language: 
- Framework: 
- Database: 
- Testing: 
- Other: 

## Development Workflow
1. [Project-specific workflow steps]

## File Structure
\`\`\`
/project-root
  /src          - [description]
  /tests        - [description]
  /docs         - [description]
\`\`\`

## Testing Strategy
[How to run tests, coverage expectations, etc.]

## Common Tasks
### Adding a new feature
[Step-by-step process]

### Debugging
[Project-specific debugging tips]

## Domain Knowledge
[Important business logic or domain concepts]

## Gotchas
[Known issues, quirks, or things to watch out for]
```

### Commit Message Template
```
type(scope): brief description

- Longer explanation if needed
- Why this change was made
- What alternatives were considered

Closes #issue-number
```

---

## Metrics for Success

Good AI collaboration should result in:
- ✅ Code that passes all tests
- ✅ Clear commit history
- ✅ Up-to-date documentation
- ✅ Minimal back-and-forth on simple tasks
- ✅ Learning and improvement over time
- ✅ Confidence in making changes

---

## Continuous Improvement

### After Each Session
- What worked well?
- What could be clearer?
- Are there patterns to capture in project `agents.md`?
- Should this workflow guide be updated?

### Periodically Review
- Is the workflow still serving the project?
- Are there new best practices to adopt?
- Can anything be simplified?
- Are there recurring issues to address?

---

*This is a living document. Update it as you learn what works best for your development style and projects.*
