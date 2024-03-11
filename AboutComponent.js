import { LitElement, html } from 'lit';

export class AboutComponent extends LitElement {
    render() {
        return html`
            <h1>About Us</h1>
            <p>This is the about page content.</p>
        `;
    }
}
customElements.define('about-component', AboutComponent);
